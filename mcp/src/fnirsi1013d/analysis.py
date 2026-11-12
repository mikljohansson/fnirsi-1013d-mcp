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


#: the delays ``core_delay_fold`` tries, in record samples: +1 designed, -1 A first, +3 core B one conversion late
CORE_DELAYS = tuple(range(-3, 6))


def core_delay_fold(raw_a: np.ndarray, raw_b: np.ndarray, divider: int, period: int,
                    delays: Sequence[int] = CORE_DELAYS) -> dict[int, float]:
    """fpga/al3/tools/core_delay_fold.py's measure, for a periodic signal of ``period`` clocks of 5 ns (prime against
    2 (divider + 1), so a core's samples cover every phase). In clocks B[k] = f(2 (D + 1) k), A[k] = f(2 (D + 1) k +
    d (D + 1)): each core folded on the period, the RMS difference of the folds for each delay d, lowest at the real
    delay. Any periodic waveform works, a sharper one gives a deeper minimum. Divider 0's is shallow (2026-11-07: 1.1 /
    1.1 / 1.2 at d = 0 / +1 / +2 on the post), so read it through ``core_delay_verdict`` over several records."""
    a = np.asarray(raw_a, np.float64)
    b = np.asarray(raw_b, np.float64)
    step = divider + 1
    k = np.arange(len(a)) * 2 * step

    def fold(x: np.ndarray, t: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        ph = t % period
        return np.bincount(ph, weights=x, minlength=period), np.bincount(ph, minlength=period)
    sb, nb = fold(b - b.mean(), k)
    out = {}
    for d in delays:
        sa, na = fold(a - a.mean(), k + d * step)
        ok = (nb > 0) & (na > 0)
        r = sb[ok] / nb[ok] - sa[ok] / na[ok]
        out[d] = float(np.sqrt(np.mean((r - r.mean()) ** 2)))
    return out


def core_delay_verdict(records: Sequence[dict[int, float]]) -> tuple[int, dict[int, int], dict[int, float]]:
    """``core_delay_fold`` over several records: (the delay with the lowest mean RMS, each record's vote, the means)."""
    mean = {d: float(np.mean([r[d] for r in records])) for d in records[0]}
    votes: dict[int, int] = {}
    for r in records:
        w = min(r, key=lambda d: r[d])
        votes[w] = votes.get(w, 0) + 1
    return min(mean, key=lambda d: mean[d]), dict(sorted(votes.items())), mean


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


#: frequency(): the 2..98 % swing must be this many times the sample noise (sigma from the second differences) for a
#: crossing to be an edge, and the rising crossings this regular (std / mean of their intervals) to be one period.
#: The battery's flat CH2 at 200 MSa/s (2026-11-06 R0b: 11 codes of noise and slope) has 5.6 and 0.45 and read 39 MHz
#: with duty 1.0; the noise-only records of that day 2..3.3 and 0.5..0.8; every real square 22 .. 4000 and below 0.004.
FREQ_MIN_SWING_SIGMA = 8.0
FREQ_MAX_INTERVAL_CV = 0.2


def noise_sigma(v: np.ndarray) -> float:
    """Sample noise (rms) of a record from the median absolute second difference: edges and slopes are outliers to
    it, so a square or a slow sine leaves it at the noise."""
    v = np.asarray(v, dtype=np.float64)
    if v.size < 5:
        return 0.0
    d2 = v[2:] - 2 * v[1:-1] + v[:-2]
    return float(np.median(np.abs(d2 - np.median(d2))) * 1.4826 / math.sqrt(6))


def frequency(v: np.ndarray, dt: float) -> dict[str, Any]:
    """Frequency, period and duty from mid-level crossings with 10 % hysteresis, or None with a ``reason``: the swing
    must stand out of the noise (FREQ_MIN_SWING_SIGMA), the record must hold a whole period (two rising crossings)
    and the crossings must be regular (FREQ_MAX_INTERVAL_CV), else noise on a flat or slow trace counts as edges."""
    v = np.asarray(v, dtype=np.float64)

    def none(reason: str) -> dict[str, Any]:
        return {"frequency": None, "period": None, "duty": None, "reason": reason}

    if v.size < 4:
        return none("too few samples")
    lo, hi = np.percentile(v, 2), np.percentile(v, 98)
    if hi - lo <= 0:
        return none("flat record")
    sig = noise_sigma(v)
    if sig > 0 and (hi - lo) < FREQ_MIN_SWING_SIGMA * sig:
        return none(f"no edges: the swing is {(hi - lo) / sig:.1f} x the sample noise (needs "
                    f"{FREQ_MIN_SWING_SIGMA:g})")
    mid = 0.5 * (lo + hi)
    hy = 0.1 * (hi - lo)
    r = crossings(v, mid, "rising", hy)
    f = crossings(v, mid, "falling", hy)
    if r.size < 2:
        return none("less than one whole period in the record")
    iv = np.diff(r)
    if iv.size > 1 and float(np.std(iv) / np.mean(iv)) > FREQ_MAX_INTERVAL_CV:
        return none(f"irregular crossings (interval spread {float(np.std(iv) / np.mean(iv)):.2f} of the mean): no "
                    "single period")
    period = float(np.mean(iv)) * dt
    duty = None
    highs = []
    for x in r[:-1]:
        nxt = f[f > x]
        if nxt.size:
            highs.append(nxt[0] - x)
    if highs and period > 0:
        duty = float(np.clip(np.mean(highs) * dt / period, 0, 1))
    return {"frequency": 1.0 / period if period > 0 else None, "period": period, "duty": duty,
            "interval_cv": float(np.std(iv) / np.mean(iv)) if iv.size > 1 else 0.0}


#: frequency_or_fft: the FFT line must have this many periods in the record; a slope or a part of one slow period
#: (the battery's RC output at 200 MSa/s, finding 4a) puts its strongest "line" in the first bins
FFT_FALLBACK_MIN_PERIODS = 4
#: frequency_or_fft: a crossing estimate is checked against the strongest spectral line when its intervals are not as
#: regular as a clean periodic signal's (every real square is below 0.004, see FREQ_MAX_INTERVAL_CV) and the two differ
#: by more than this share of the line's frequency (and two bins). Bench 2026-11-12: a 483.6 kHz buck ripple with
#: switching spikes at 20 MSa/s read 509.7 kHz from crossings (spread under 0.2, so accepted), 483.6 kHz in the FFT
FREQ_CHECK_MIN_CV = 0.02
FREQ_CHECK_MAX_DIFF = 0.02


def frequency_or_fft(v: np.ndarray, dt: float) -> dict[str, Any]:
    """``frequency`` with a fallback: when the crossings are irregular (switching spikes on a ripple, a burst), the
    strongest spectral line (``dominant_frequency``) of the same record, ``method`` 'fft_peak' with the bin width as
    ``resolution`` and the crossing ``reason`` kept, when that line has at least FFT_FALLBACK_MIN_PERIODS periods
    in the record; duty is None then. The same line replaces a crossing estimate whose intervals are uneven
    (FREQ_CHECK_MIN_CV) and which differs from it (FREQ_CHECK_MAX_DIFF); ``crossings_frequency`` keeps the crossing
    value. ``method`` is 'crossings' for a crossing
    estimate and None when there is no frequency."""
    fr = frequency(v, dt)
    if fr["frequency"] is not None:
        cv = fr.pop("interval_cv", 0.0)
        if cv > FREQ_CHECK_MIN_CV:
            f = dominant_frequency(v, dt)
            n = np.asarray(v).size
            res = 1.0 / (n * dt)
            if (f is not None and f * n * dt >= FFT_FALLBACK_MIN_PERIODS and
                    abs(fr["frequency"] - f) > max(FREQ_CHECK_MAX_DIFF * f, 2 * res)):
                return {"frequency": f, "period": 1.0 / f, "duty": None, "method": "fft_peak", "resolution": res,
                        "crossings_frequency": fr["frequency"],
                        "reason": f"crossings disagree with the spectrum: they give {fr['frequency']:.6g} Hz with an "
                                  f"interval spread of {cv:.2f} of the mean (extra or missing crossings)"}
        return {**fr, "method": "crossings"}
    if str(fr.get("reason", "")).startswith("irregular crossings"):
        f = dominant_frequency(v, dt)
        n = np.asarray(v).size
        if f is not None and f * n * dt >= FFT_FALLBACK_MIN_PERIODS:
            return {"frequency": f, "period": 1.0 / f, "duty": None, "method": "fft_peak",
                    "resolution": 1.0 / (n * dt), "reason": fr["reason"]}
        fr = {**fr, "reason": fr["reason"] + "; no spectral line stands out either"}
    return {**fr, "method": None}


def moving_average(v: np.ndarray, n: int) -> np.ndarray:
    """Centred moving average over ``n`` samples (made odd); the window shrinks symmetrically at the record ends."""
    v = np.asarray(v, dtype=np.float64)
    n = int(n)
    if n <= 1 or v.size < 3:
        return v.copy()
    h = min(n // 2, (v.size - 1) // 2)
    c = np.concatenate([[0.0], np.cumsum(v)])
    i = np.arange(v.size)
    hw = np.minimum(np.minimum(i, v.size - 1 - i), h)
    return (c[i + hw + 1] - c[i - hw]) / (2 * hw + 1)


#: level_crossings: hysteresis in units of the (smoothed) sample noise, and how close (in noise sigmas) a level may lie
#: to the record's extremes before its crossings count as noise-dominated
CROSS_HYST_SIGMA = 3.0
CROSS_NEAR_SIGMA = 4.0


def _interp_cross(v: np.ndarray, i: int, level: float) -> float:
    a, b = v[i], v[i + 1]
    return i + ((level - a) / (b - a) if b != a else 0.5)


def hysteresis_crossings(v: np.ndarray, level: float, edge: str, hyst: float) -> list[tuple[float, float, float]]:
    """Crossings of ``level`` in one direction with hysteresis: armed once ``v`` is beyond level -/+ ``hyst`` on the
    start side, confirmed once it is beyond level +/- ``hyst`` on the far side. Between the two, noise may cross the
    level several times: the crossing is the midpoint of the first and the last of those (fractional sample indices,
    linear interpolation), which a noisy slope does not bias towards either end. Returns (mid, first, last)."""
    v = np.asarray(v, dtype=np.float64)
    s = 1.0 if edge == "rising" else -1.0
    u = s * (v - level)  # rising: from below (u < 0) to above (u > 0)
    out = []
    armed = False
    first = -1.0
    last = -1.0
    for i in range(v.size - 1):
        if not armed:
            if u[i] >= -hyst:
                continue
            armed = True
        if u[i] < 0 <= u[i + 1]:
            x = _interp_cross(v, i, level)
            if first < 0:
                first = x
            last = x
        if first >= 0 and u[i + 1] > hyst:
            out.append((0.5 * (first + last), first, last))
            armed, first, last = False, -1.0, -1.0
    return out


def level_crossings(t: np.ndarray, v: np.ndarray, levels: Sequence[float], edge: str = "either", smooth: int = 1,
                    max_per_level: int = 16, lsb: float | None = None,
                    between: tuple[float, float] | None = None) -> dict[str, Any]:
    """The times (on ``t``'s axis, interpolated between samples) at which ``v`` crosses each level.

    ``edge``: 'rising', 'falling' or 'either'. ``smooth``: a centred moving average over that many samples first.
    The hysteresis is ``CROSS_HYST_SIGMA`` x the smoothed record's sample noise (at least half an ``lsb``): a crossing
    is the midpoint of the noise's crossings between leaving level -/+ hysteresis and passing level +/- hysteresis
    (``hysteresis_crossings``), so noise on a slow edge gives one unbiased crossing, not a burst. Each crossing carries
    the slope (over a span wide enough to stand out of the noise) and ``t_sigma``: the larger of noise / |slope| and
    half the span of the noise's crossings. At most ``max_per_level`` crossings per level are returned (``count`` has
    them all). ``between`` = (from, to): the time from the first crossing of ``from`` to the first crossing of ``to``
    after it. Returns ``levels`` (one dict per level), ``noise`` (volts, smoothed), ``hysteresis``, ``smooth`` and
    ``caveats``."""
    t = np.asarray(t, dtype=np.float64)
    v = np.asarray(v, dtype=np.float64)
    if v.size < 3 or t.size != v.size:
        raise ValueError("level crossings need a record of at least 3 samples with its time axis")
    if edge not in ("rising", "falling", "either"):
        raise ValueError(f"edge must be rising, falling or either (got {edge!r})")
    dt = float(t[1] - t[0])
    smooth = max(1, int(smooth))
    if smooth % 2 == 0:
        smooth += 1
    vs = moving_average(v, smooth)
    sigma = noise_sigma(v) / math.sqrt(smooth)
    if lsb:
        sigma = max(sigma, lsb / math.sqrt(12 * smooth))
    hyst = max(CROSS_HYST_SIGMA * sigma, 0.5 * lsb if lsb else 0.0)
    lo, hi = float(vs.min()), float(vs.max())
    idx = np.arange(t.size)
    edges = ("rising", "falling") if edge == "either" else (edge,)

    def find(lv: float) -> list[tuple[float, float, float, str]]:
        f = [(m, a, b, e) for e in edges for m, a, b in hysteresis_crossings(vs, lv, e, hyst)]
        f.sort()
        return f

    def slope_at(x: float) -> float:
        i = int(x)
        w = max(1, smooth // 2)
        while True:
            a, b = max(0, i - w), min(v.size - 1, i + 1 + w)
            if abs(vs[b] - vs[a]) >= 6 * sigma or (a == 0 and b == v.size - 1) or w > v.size // 8:
                return float((vs[b] - vs[a]) / ((b - a) * dt))
            w *= 2

    caveats: list[str] = []
    out_levels = []
    found_by: dict[float, list[tuple[float, float, float, str]]] = {}
    for lv in levels:
        lv = float(lv)
        found = found_by[lv] = find(lv)
        rows = []
        for m, a, b, e in found[:max_per_level]:
            slope = slope_at(m)
            ts = max(sigma / abs(slope) if slope else math.inf, 0.5 * (b - a) * dt)
            rows.append({"t": float(np.interp(m, idx, t)), "edge": e, "slope_v_per_s": slope,
                         "t_sigma": ts if math.isfinite(ts) else None})
        d: dict[str, Any] = {"level": lv, "count": len(found), "crossings": rows,
                             "first": rows[0]["t"] if rows else None}
        if len(found) > max_per_level:
            d["truncated"] = True
        out_levels.append(d)
        if not found:
            if lv < lo or lv > hi:
                caveats.append(f"level {lv:.6g} V is never reached: the record spans {lo:.6g} .. {hi:.6g} V")
            else:
                caveats.append(f"level {lv:.6g} V is not crossed{'' if edge == 'either' else ' ' + edge} beyond the "
                               f"hysteresis ({hyst:.3g} V)")
        elif len(found) > 1:
            caveats.append(f"level {lv:.6g} V is crossed {len(found)} times"
                           f"{f' (the first {max_per_level} returned)' if len(found) > max_per_level else ''}: "
                           "ringing, noise or a periodic signal; smooth more or narrow the edge direction")
        if found and (lv - lo < CROSS_NEAR_SIGMA * sigma or hi - lv < CROSS_NEAR_SIGMA * sigma):
            caveats.append(f"level {lv:.6g} V lies within {CROSS_NEAR_SIGMA:g} noise sigmas ({sigma:.3g} V) of the "
                           "record's extreme: its crossing time is set by the noise; see t_sigma")
    res: dict[str, Any] = {"levels": out_levels, "noise": sigma, "hysteresis": hyst, "smooth": smooth,
                           "caveats": caveats}
    if between is not None:
        a_lv, b_lv = float(between[0]), float(between[1])
        for lv in (a_lv, b_lv):
            if lv not in found_by:
                found_by[lv] = find(lv)
        bt: dict[str, Any] = {"from_level": a_lv, "to_level": b_lv, "t_from": None, "t_to": None, "interval": None}
        if found_by[a_lv]:
            bt["t_from"] = float(np.interp(found_by[a_lv][0][0], idx, t))
            later = [float(np.interp(m, idx, t)) for m, *_ in found_by[b_lv]]
            later = [x for x in later if x >= bt["t_from"]]
            if later:
                bt["t_to"] = later[0]
                bt["interval"] = later[0] - bt["t_from"]
            else:
                bt["reason"] = f"no crossing of {b_lv:.6g} V after the first crossing of {a_lv:.6g} V"
        else:
            bt["reason"] = f"{a_lv:.6g} V is not crossed"
        res["between"] = bt
    return res


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


#: 0x1535 CIC compensation FIR taps h[0..7] per K (fpga/al3/src/zaklad.v fir_chan; symmetric, 15 taps, sum 2^17)
FIR_TAPS_HALF = {1: (-82, 682, 1890, -2151, -9088, 3022, 40046, 62434),
                 2: (-231, 504, 2397, -1457, -10255, 877, 40852, 65698),
                 3: (-381, 332, 2919, -766, -11452, -1289, 41675, 68996)}


def fir_taps(k: int) -> np.ndarray:
    """The 15 FIR taps for CIC order k, normalised to DC gain 1."""
    h = FIR_TAPS_HALF[max(1, min(3, k))]
    return np.array(h + h[-2::-1], dtype=np.float64) / (1 << 17)


def decim_kernel(k: int, m: int, fir: bool) -> np.ndarray:
    """Impulse response at the raw 200 MSa/s rate, DC gain 1: K-fold boxcar of m (the CIC), then with ``fir`` the
    15-tap FIR at the CIC output rate (taps m raw samples apart)."""
    w = np.ones(m) / m
    out = w
    for _ in range(k - 1):
        out = np.convolve(out, w)
    if fir:
        up = np.zeros(14 * m + 1)
        up[::m] = fir_taps(k)
        out = np.convolve(out, up)
    return out


def decim_noise_n(k: int, n: int, fir: bool = False) -> float:
    """Noise-equivalent number of averaged samples of a K-fold boxcar of N (1 / sum of the squared normalised
    weights): N for a boxcar, 3N^3 / (2N^2 + 1) for CIC2, about 20N / 11 for CIC3. With the 1.11 FIR the CIC runs
    on N / 2 and the FIR follows (computed numerically, scaled from N / 2 <= 512)."""
    if k <= 0 or n <= 1:
        return 1.0
    if fir:
        m = n // 2
        mm = min(m, 512)
        w = decim_kernel(k, mm, True)
        return float(1.0 / np.sum(w * w)) * m / mm
    if k == 1:
        return float(n)
    if k == 2:
        return 3.0 * n ** 3 / (2.0 * n * n + 1.0)
    if n <= 4096:
        w = np.ones(n) / n
        w3 = np.convolve(np.convolve(w, w), w)
        return float(1.0 / np.sum(w3 * w3))
    return 20.0 * n / 11.0


def decim_bits(k: int, n: int, gain: int, shift: int, noise_lsb: float = 1.0, fir: bool = False) -> dict[str, float]:
    """Resolution of a decimated sample in bits re the 8-bit range: ``storage`` = 8 + log2(N^K / 2^S) (the stored
    step is 2^S / N^K codes, at most 16 bits), ``averaging`` = 8 + log2(sqrt(noise-equivalent N)) while the raw noise
    dithers (>= ~0.5 LSB rms), ``effective`` = the smaller."""
    if k <= 0:
        return {"storage": 8.0, "averaging": 8.0, "effective": 8.0}
    storage = 8.0 + math.log2(gain / (1 << shift))
    averaging = 8.0 + 0.5 * math.log2(decim_noise_n(k, n, fir)) if noise_lsb >= 0.5 else 8.0
    return {"storage": storage, "averaging": averaging, "effective": min(storage, averaging)}


# ---------------------------------------------------------------------------------------------------------
# Mask testing (scope_mask_test)


def mask_envelope(ref: np.ndarray, dv: float, k: int) -> tuple[np.ndarray, np.ndarray]:
    """Upper and lower limit around a reference record: the running max / min over +-k samples (the time tolerance)
    widened by dv volts (the amplitude tolerance). Edge samples are held."""
    ref = np.asarray(ref, dtype=np.float64)
    if k > 0:
        pad = np.pad(ref, k, mode="edge")
        win = np.lib.stride_tricks.sliding_window_view(pad, 2 * k + 1)
        hi, lo = win.max(axis=1), win.min(axis=1)
    else:
        hi = lo = ref
    return hi + dv, lo - dv


def mask_check(v: np.ndarray, upper: np.ndarray, lower: np.ndarray, guard: int = 0) -> dict[str, Any]:
    """Compare one record with the mask: the failing samples, the first one and the worst excursion in volts.
    ``guard`` samples at each end are not judged: there the time tolerance would need the reference before its first
    (or after its last) sample, so an edge that the trigger jitter moves into them fails against a held edge value
    (battery 2026-11-06 follow-up 4)."""
    v = np.asarray(v, dtype=np.float64)
    over = v - upper
    under = lower - v
    exc = np.maximum(over, under)
    if guard > 0:
        g = min(guard, exc.size)
        exc[:g] = -np.inf
        exc[exc.size - g:] = -np.inf
    fail = exc > 0
    n = int(fail.sum())
    out: dict[str, Any] = {"passed": n == 0, "fail_samples": n, "fail_mask": fail}
    if n:
        first = int(np.argmax(fail))
        worst = int(np.argmax(exc))
        out["first_fail"] = {"index": first, "volts": float(v[first]),
                             "bound": "upper" if over[first] > 0 else "lower",
                             "limit": float(upper[first] if over[first] > 0 else lower[first])}
        out["worst_excursion_volts"] = float(exc[worst])
        out["worst_index"] = worst
    return out


# ---------------------------------------------------------------------------------------------------------
# DVM readout (scope_dvm)


def dominant_frequency(v: np.ndarray, dt: float) -> float | None:
    """Frequency of the strongest spectral component (Hann window, parabolic peak), None when nothing stands 6x
    (about 16 dB) above the median bin: the largest of 16k noise bins reaches about 3.6x the median."""
    v = np.asarray(v, dtype=np.float64)
    if v.size < 16:
        return None
    f, amp, _ = amplitude_spectrum(v, dt, "hann", detrend=True)
    floor = float(np.median(amp[1:]))
    pk = peaks(f, amp, 1, floor=floor * 6 if floor > 0 else None)
    return pk[0][0] if pk else None


def dvm_summary(readings: dict[str, list[float]]) -> dict[str, float | None]:
    """Combine per-capture statistics into one reading: DC = mean of the means (and their spread), AC / true RMS as
    power means, the extremes, Vpp from them, the median dominant frequency."""
    means = np.asarray(readings["mean"], dtype=np.float64)
    n = means.size
    ac = np.asarray(readings["ac_rms"], dtype=np.float64)
    rms = np.asarray(readings["rms"], dtype=np.float64)
    freqs = [f for f in readings.get("frequency", []) if f is not None and math.isfinite(f)]
    vmin, vmax = float(np.min(readings["min"])), float(np.max(readings["max"]))
    return {"dc_mean": float(means.mean()), "dc_std_between_captures": float(means.std(ddof=1)) if n > 1 else None,
            "ac_rms": float(np.sqrt(np.mean(ac * ac))), "rms": float(np.sqrt(np.mean(rms * rms))),
            "min": vmin, "max": vmax, "vpp": vmax - vmin,
            "frequency": float(np.median(freqs)) if freqs else None, "n_captures": n}


def dvm_resolution(step_volts: float, full_scale_volts: float, noise_codes: float, n_samples: int,
                   dc_std_between: float | None, n_captures: int) -> dict[str, Any]:
    """What the DC reading resolves: the 1-sigma uncertainty of the mean from the sample noise (needs dither: below
    0.5 code rms the mean cannot resolve below one code, so the step bounds it), from the spread between captures
    when there are several, and the quantisation floor; ``digits`` = log10(full scale / uncertainty)."""
    if noise_codes < 0.5:
        u_noise = step_volts / 2
        dither = False
    else:
        u_noise = noise_codes * step_volts / math.sqrt(max(n_samples, 1))
        dither = True
    u_q = step_volts / math.sqrt(12 * max(n_samples, 1))
    u = max(u_noise, u_q)
    if dc_std_between is not None and n_captures > 1:
        u = max(u, dc_std_between / math.sqrt(n_captures))
    digits = math.log10(full_scale_volts / u) if u > 0 and full_scale_volts > 0 else 0.0
    return {"uncertainty_volts": u, "digits": digits, "dither": dither, "step_volts": step_volts,
            "full_scale_volts": full_scale_volts, "noise_codes": noise_codes, "n_samples": n_samples}


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
