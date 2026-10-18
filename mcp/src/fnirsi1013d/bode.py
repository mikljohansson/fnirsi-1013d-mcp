"""Host-driven Bode / frequency-response sweep (docs/plans/bode-design.md steps H1-H3).

Stepped single tone: the calibration-pin DDS runs at f = 50 MHz / M for an integer M (clean edges, no pattern tones),
CH1 sits on the DUT input, CH2 on its output, and H(f) = X2 / X1 from a single-bin DFT at the exact DDS frequency
f_a = round(2^32 / M) x 50 MHz / 2^32 over a coherent record (an integer number of stimulus periods), so the square
wave's harmonics land on other bins and drop out. Rate per point (design 3.2): the 0x1535 CIC3 + FIR at
N = 16 x 2^k (FIR-legal: N >= 16, N % 4 == 0) with 257..512 samples per period below 24 kHz, N = 16 (12.5 MSa/s) up to
1.5 MHz, legacy 200 MSa/s above.

Pieces, from pure to hardware:

- ``plan_point`` / ``plan_sweep``: the deterministic frequency plan (M, N, samples per period p / q, record L,
  settle time), shared with the later on-device engine.
- ``tone`` / ``measure_pair``: the DFT, H, and the per-point quality numbers (SNR from the record's residual bins,
  leakage next to the fundamental = coherence, the half-record settle check, clipping, CH1 level).
- ``markers``: -3 dB crossings, peak and Q, phase crossings.
- ``ThruCal``: a sweep with both probes on the DUT input, stored and divided out of later sweeps.
- ``BodeRunner``: drives a ``Scope`` (generator, divider, filter, trigger, channels), with CH1 / CH2 ranging, the
  offset centring, the background (generator off) capture per rate and CH2 range, CH2 down-ranging with the splice,
  E_BUSY retry, and gives every setting back as it found it.
- ``main``: the ``fnirsi1013d-bode`` CLI.
"""

from __future__ import annotations

import csv
import json
import math
from dataclasses import dataclass, field
from fractions import Fraction
from pathlib import Path
from typing import Any

import numpy as np

from . import protocol as P

GEN_CLOCK_HZ = P.GEN_CLOCK_HZ  # DDS clock, 50 MHz
ADC_CLOCK_HZ = P.BASE_CLOCK_HZ  # 200 MSa/s per channel
#: above this the plan uses the legacy 200 MSa/s capture (design 3.2)
LEGACY_ABOVE_HZ = 1.5e6
#: samples per period target below 24 kHz (257..512)
MAX_SPP = 512
MIN_PERIODS = 8
MIN_RECORD = 2048
#: the trigger total limit with the filter on (protocol row 47); legacy points stay under it too
MAX_RECORD = P.DECIM_MAX_TOTAL
MIN_FREQ_HZ = 1.0
MAX_FREQ_HZ = 10e6
#: coherence error (periods) above which the DFT uses a Hann window (below about 50 Hz for some M)
HANN_ABOVE_PERIODS = 1e-3
#: settle presets in stimulus periods before the record (design 3.3)
SETTLE_PRESETS = {"fast": 3, "normal": 10, "resonant": 50, "high_q": 200}
FIRST_POINT_SETTLE_FACTOR = 3
MIN_SETTLE_S = 1e-3
#: host overhead per point (SET_GEN, ARM, WAIT, READ_RAW of 2 x 4096 16-bit samples, Python), design 3.7
HOST_OVERHEAD_S = 0.04
RELAY_SETTLE_S = 0.05

# quality gates (design 3.3, 3.5, 3.6, section 4 warnings)
CH1_LOW_CODES = 20.0  # CH1 fundamental below this (codes peak): "ch1_low"
CH2_SMALL_CODES = 3.0  # CH2 fundamental below this: INL dominates (+-1 dB): "ch2_small"
NEAR_FLOOR_DB = 20.0  # CH2 less than this above the background in its bin: "near_floor"
LOW_SNR_DB = 20.0  # CH2 fundamental less than this above the record's noise per bin: "low_snr"
LEAK_LIMIT_DB = -40.0  # CH1 level one bin beside the fundamental re the fundamental: above = "incoherent"
SETTLE_DB = 0.1  # half-record check: halves differ by more than 0.1 dB or 1 deg -> recapture, then "unsettled"
SETTLE_DEG = 1.0
FULL_SCALE_FRACTION = 0.85  # ranging: the most sensitive range that keeps the peak inside 85 % of full scale
DOWNRANGE_FRACTION = 0.6  # down-range when the predicted peak on the next range stays under 60 % of full scale
CENTRE_TOLERANCE_CODES = 12.0  # re-centre the offset when the record's mean is further than this from 128
MAX_RANGE_SWITCHES = 6  # CH2 auto-range budget per sweep (3 for a monotonic filter, 2 more for a resonance)

#: flags that make a point unusable for markers (drawn grey by the on-device view)
BAD_FLAGS = frozenset({"ch1_clipped", "clipped", "unsettled", "incoherent", "near_floor", "low_snr"})

#: wiring the person must use (design 2.5); shown with the first sweep of a session
CONNECTION_TEXT = (
    "Wiring: calibration post -> DUT input, CH1 probe tip on the DUT input; CH2 probe tip on the DUT output. "
    "Source: the post is a 3.1 Vpp square through 1 kOhm (R21); H = CH2 / CH1, so CH1 must touch the DUT input. "
    "Ground: connect the DUT ground to the scope's USB ground (USB connector shell or a USB breakout's GND) and leave "
    "both probe ground clips OPEN. A DUT returned through a probe ground clip pushes the stimulus current through "
    "R71 (100 Ohm) into the probe reference (VRF1) and adds a common error to both channels (about 10 % at 100 kHz "
    "for a 1.6 kOhm RC, worse at low frequency). Clips on a node tied to USB ground load VRF1 and shift every trace. "
    "Passive DUTs or buffered inputs only: the post has no protection beyond the 1 kOhm (never a coil, a MOSFET "
    "drain or anything that can drive the post). DC coupling on both channels (the tool sets it).")


class BodeError(Exception):
    pass


# ---------------------------------------------------------------------------------------------------------
# Plan


def parse_settle(settle: str | float | int | None) -> float:
    """A settle preset name or a number of stimulus periods."""
    if settle is None:
        return float(SETTLE_PRESETS["normal"])
    if isinstance(settle, (int, float)):
        if settle < 0:
            raise ValueError(f"settle {settle} periods < 0")
        return float(settle)
    s = str(settle).strip().lower().replace("-", "_").replace(" ", "_")
    if s in SETTLE_PRESETS:
        return float(SETTLE_PRESETS[s])
    try:
        v = float(s)
    except ValueError:
        raise ValueError(f"settle {settle!r}: use {', '.join(SETTLE_PRESETS)} or a number of periods") from None
    return parse_settle(v)


def phase_inc_for(m: int) -> int:
    """round(2^32 / M), the SET_GEN raw phase increment for a period of M DDS clocks."""
    return ((1 << 32) + m // 2) // m


def dds_frequency(phase_inc: int) -> float:
    return phase_inc * GEN_CLOCK_HZ / 2 ** 32


@dataclass(frozen=True)
class PointPlan:
    """One frequency point (design 3.2)."""

    f_requested: float
    #: stimulus period in 50 MHz DDS clocks
    m: int
    #: decimation factor (1 = legacy 200 MSa/s)
    n: int
    phase_inc: int
    #: the frequency the DFT uses: phase_inc x 50 MHz / 2^32
    f_actual: float
    #: samples per period 4M / N = p / q in lowest terms
    p: int
    q: int
    #: record length (samples analysed) and the number of p-blocks in it
    record: int
    blocks: int

    @property
    def legacy(self) -> bool:
        return self.n == 1

    @property
    def divider(self) -> int:
        """SET_TIMEBASE raw divider: N - 1 (for even N the filter's 2 (floor(D / 2) + 1) = N), 0 for legacy."""
        return 0 if self.legacy else self.n - 1

    @property
    def sample_rate(self) -> float:
        return ADC_CLOCK_HZ / self.n

    @property
    def periods(self) -> int:
        """Nominal stimulus periods in the record (exact for the planned M; f_actual differs by the inc rounding)."""
        return self.blocks * self.q

    @property
    def total_samples(self) -> int:
        """The trigger total for the capture (even, >= record)."""
        return self.record + (self.record & 1)

    @property
    def record_s(self) -> float:
        return self.total_samples / self.sample_rate

    @property
    def halves(self) -> bool:
        """Whether each half of the record holds whole periods (even number of p-blocks): the settle check."""
        return self.blocks % 2 == 0

    @property
    def coherence_error(self) -> float:
        """Periods by which the record misses a whole number at the real DDS frequency (the 32-bit increment
        rounding: up to 0.5 % at 1 Hz, below 1e-5 above 1 kHz)."""
        return abs(self.record * self.f_actual / self.sample_rate - self.periods)

    @property
    def window(self) -> str:
        """'rect' for a coherent record, 'hann' where the increment rounding leaves it more than 1e-3 periods off."""
        return "hann" if self.coherence_error > HANN_ABOVE_PERIODS else "rect"

    @property
    def fir_expected(self) -> bool:
        return P.decim_fir_eligible(self.n) if self.n > 1 else False

    def filter_settle_s(self, k: int = 3) -> float:
        """The FPGA filter's settling after a divider change ((K + 10) N 5 ns + 1 us, protocol row 49)."""
        return 0.0 if self.legacy else (k + 10) * self.n * 5e-9 + 1e-6

    def settle_s(self, periods: float) -> float:
        return max(periods / self.f_actual, MIN_SETTLE_S)


def plan_point(f_req: float) -> PointPlan:
    """The plan for one requested frequency (design 3.2)."""
    if not MIN_FREQ_HZ <= f_req <= MAX_FREQ_HZ:
        raise ValueError(f"frequency {f_req:g} Hz outside {MIN_FREQ_HZ:g} Hz .. {MAX_FREQ_HZ:g} Hz")
    m0 = max(2, int(round(GEN_CLOCK_HZ / f_req)))
    if GEN_CLOCK_HZ / m0 > LEGACY_ABOVE_HZ:
        n, m = 1, m0
    else:
        n = 16
        while 4 * m0 > MAX_SPP * n:
            n *= 2
        # below 24 kHz the samples per period must be an integer S (else p keeps M's odd part and no record fits)
        m = m0 if n == 16 else int(round(4 * m0 / n)) * n // 4
    spp = Fraction(4 * m, n)
    p, q = spp.numerator, spp.denominator
    j = max(math.ceil(MIN_PERIODS / q), math.ceil(MIN_RECORD / p))
    if j % 2 and (j + 1) * p <= MAX_RECORD:
        j += 1  # an even number of blocks: the halves hold whole periods (settle check)
    if j * p > MAX_RECORD:
        raise ValueError(f"no coherent record for {f_req:g} Hz (M {m}, N {n}, p {p})")  # cannot happen in range
    inc = phase_inc_for(m)
    return PointPlan(f_req, m, n, inc, dds_frequency(inc), p, q, j * p, j)


def log_points(start: float, stop: float, points: int) -> list[float]:
    if points < 1:
        raise ValueError("points must be >= 1")
    if start <= 0 or stop <= 0:
        raise ValueError("start and stop must be > 0")
    if points == 1:
        return [float(start)]
    return [float(x) for x in np.geomspace(start, stop, points)]


def plan_sweep(start: float, stop: float, points: int) -> list[PointPlan]:
    """Log-spaced points from start to stop (either order; measured low to high), snapped to integer M, merged when
    two requests give the same M (design 3.1)."""
    lo, hi = sorted((float(start), float(stop)))
    out: list[PointPlan] = []
    for f in log_points(lo, hi, points):
        pp = plan_point(f)
        if out and out[-1].m == pp.m:
            continue
        out.append(pp)
    return out


def estimate_duration(plans: list[PointPlan], settle_periods: float, background: bool = True,
                      ch2_auto: bool = True) -> float:
    """Seconds for a sweep (design 3.7): per point overhead + settle + record, the first point's longer settle, the
    pre-scan, background captures per rate and a few relay switches."""
    if not plans:
        return 0.0
    t = 0.0
    for i, pp in enumerate(plans):
        k = FIRST_POINT_SETTLE_FACTOR if i == 0 else 1
        t += HOST_OVERHEAD_S + pp.settle_s(settle_periods * k) + pp.record_s + pp.filter_settle_s()
    t += 2 * (HOST_OVERHEAD_S + plans[0].record_s) + 4 * RELAY_SETTLE_S  # pre-scan and the centring check
    if background:
        groups: dict[int, PointPlan] = {}
        for pp in plans:
            groups.setdefault(pp.n, pp)
        for pp in groups.values():
            t += HOST_OVERHEAD_S + pp.record_s + pp.settle_s(settle_periods * FIRST_POINT_SETTLE_FACTOR)
    if ch2_auto:
        t += 3 * (RELAY_SETTLE_S + HOST_OVERHEAD_S + plans[-1].record_s)
    return t


#: requests in the plan vectors (docs/bode-plan-vectors.json): the band edges and a log sweep
VECTOR_FREQS = (1.0, 1.7, 10.0, 23.9, 24.5, 100.0, 999.0, 1000.0, 1234.5, 24e3, 24.4e3, 25e3, 100e3, 333e3, 1e6, 1.49e6,
                1.5e6, 1.6e6, 2e6, 5e6, 10e6)


def plan_vectors() -> dict[str, Any]:
    """The planner's output for VECTOR_FREQS and two sweeps: what the on-device engine (design step D1) must match."""
    def row(pp: PointPlan) -> dict[str, Any]:
        return {"f_req": pp.f_requested, "m": pp.m, "n": pp.n, "divider": pp.divider, "phase_inc": pp.phase_inc,
                "f_mhz": int(round(pp.f_actual * 1000)), "p": pp.p, "q": pp.q, "record": pp.record,
                "total": pp.total_samples}

    def sweep(a: float, b: float, n: int) -> dict[str, Any]:
        return {"start": a, "stop": b, "points": n, "plan": [row(pp) for pp in plan_sweep(a, b, n)]}

    return {"format": "fnirsi1013d-bode-plan", "version": 1,
            "notes": "mcp/src/fnirsi1013d/bode.py plan_point / plan_sweep (docs/plans/bode-design.md 3.1-3.2). "
                     "f_req in Hz (sweeps: np.geomspace), f_mhz = round(phase_inc x 50e9 / 2^32), divider = N - 1 "
                     "(0 = legacy 200 MSa/s), record = samples analysed, total = the even trigger total. "
                     "Regenerate: python -m fnirsi1013d.bode --write-vectors ../docs/bode-plan-vectors.json",
            "points": [row(plan_point(f)) for f in VECTOR_FREQS],
            "sweeps": [sweep(100.0, 1e6, 50), sweep(10.0, 100e3, 20), sweep(1e6, 10e6, 30)]}


def plan_summary(plans: list[PointPlan]) -> dict[str, Any]:
    ns = sorted({pp.n for pp in plans})
    return {"points": len(plans), "f_first": plans[0].f_actual if plans else None,
            "f_last": plans[-1].f_actual if plans else None, "decimation_factors": ns,
            "sample_rates": [ADC_CLOCK_HZ / n for n in ns],
            "legacy_points": sum(pp.legacy for pp in plans)}


# ---------------------------------------------------------------------------------------------------------
# DSP


def tone(x: np.ndarray, f: float, fs: float, window: str = "rect") -> complex:
    """Peak amplitude and phase (cosine reference at sample 0) of the component at f: mean removed,
    X = 2 / sum(w) sum w_n (x_n - mean) e^{-j 2 pi f n / fs}. ``window`` 'rect' (coherent records) or 'hann' (records
    that are not a whole number of periods: the harmonics' leakage falls 18 dB per octave instead of 6)."""
    x = np.asarray(x, np.float64)
    n = np.arange(x.size)
    ph = np.exp(-2j * np.pi * (f / fs) * n)
    if window == "hann":
        w = 0.5 - 0.5 * np.cos(2 * np.pi * (n + 0.5) / x.size)
        xm = x - np.dot(w, x) / w.sum()
        return complex(2.0 / w.sum() * np.dot(w * xm, ph))
    return complex(2.0 / x.size * np.dot(x - x.mean(), ph))


def residual_noise(x: np.ndarray, periods: float) -> float:
    """Noise amplitude (same scale as ``tone``) of one bin, from the record's spectrum without DC and the bins at
    and beside the harmonics of the stimulus: the median bin power (robust to isolated spurs) / ln 2."""
    x = np.asarray(x, np.float64)
    L = x.size
    y = np.fft.rfft(x - x.mean())
    k = np.arange(y.size)
    c = max(1, int(round(periods)))
    near = np.abs(((k + c / 2) % c) - c / 2) <= 1 if c > 2 else np.zeros(y.size, bool)
    keep = (k > 1) & ~near
    if keep.sum() < 8:
        keep = k > 1
    pw = np.abs(y[keep]) ** 2
    if pw.size == 0:
        return 0.0
    return float(2.0 / L * math.sqrt(np.median(pw) / math.log(2)))


def db(x: float) -> float:
    return 20.0 * math.log10(x) if x > 0 else -math.inf


def wrap_deg(d: float) -> float:
    """Wrap to (-180, 180]."""
    w = (d + 180.0) % 360.0 - 180.0
    return 180.0 if w == -180.0 else w


@dataclass
class Measurement:
    """One capture of one point analysed: complex fundamentals (volts peak), quality numbers."""

    x1: complex
    x2: complex
    noise1: float
    noise2: float
    leak_db: float
    #: complex relative difference of H between the record halves (None: odd number of blocks)
    half_rel: float | None
    half_db: float | None
    half_deg: float | None
    clipped1: bool
    clipped2: bool
    #: fundamentals in codes peak (volts / volts per code)
    codes1: float
    codes2: float
    #: record means (volts), the largest distance of a sample from the record mean (codes) and the mean code
    mean1: float
    mean2: float
    ac1: float
    ac2: float
    centre1: float
    centre2: float
    #: the capture's decimation (None = legacy) as captured
    filter_effective: str = "legacy"
    fir: bool = False

    @property
    def h(self) -> complex:
        return self.x2 / self.x1 if self.x1 else complex("nan")

    @property
    def snr2_db(self) -> float:
        return db(abs(self.x2) / self.noise2) if self.noise2 > 0 else math.inf

    @property
    def unsettled(self) -> bool:
        if self.half_rel is None or self.half_db is None or self.half_deg is None:
            return False
        if abs(self.half_db) <= SETTLE_DB and abs(self.half_deg) <= SETTLE_DEG:
            return False
        # noise alone moves each half by about sqrt(2) x noise / |X|; flag only a difference well above it
        noise_rel = math.sqrt(2) * math.hypot(self.noise1 / abs(self.x1) if self.x1 else 0.0,
                                              self.noise2 / abs(self.x2) if self.x2 else 0.0)
        return self.half_rel > 4.0 * math.sqrt(2) * noise_rel


def measure_pair(v1: np.ndarray, v2: np.ndarray, pp: PointPlan, *, vpl1: float, vpl2: float,
                 clipped1: np.ndarray | bool = False, clipped2: np.ndarray | bool = False,
                 codes1: np.ndarray | None = None, codes2: np.ndarray | None = None,
                 filter_effective: str = "legacy", fir: bool = False) -> Measurement:
    """Analyse the first ``pp.record`` samples of a CH1 / CH2 record (volts at the probe tips). ``vpl*`` are volts
    per code (for the code levels), ``codes*`` the 8-bit-scale codes for the excursion check (default: derived from
    the volts around the mean)."""
    L = pp.record
    if len(v1) < L or len(v2) < L:
        raise BodeError(f"record of {min(len(v1), len(v2))} samples < plan {L}")
    a = np.asarray(v1[:L], np.float64)
    b = np.asarray(v2[:L], np.float64)
    fs, f, win = pp.sample_rate, pp.f_actual, pp.window
    x1, x2 = tone(a, f, fs, win), tone(b, f, fs, win)
    periods = L * f / fs
    n1, n2 = residual_noise(a, periods), residual_noise(b, periods)
    if win == "hann":  # the window's noise bandwidth: 1.5 bins
        n1, n2 = n1 * math.sqrt(1.5), n2 * math.sqrt(1.5)
    d = fs / L * (2 if win == "hann" else 1)  # the first zero of the window's kernel beside the fundamental
    side = max(abs(tone(a, f - d, fs, win)), abs(tone(a, f + d, fs, win)))
    leak = db(side / abs(x1)) if x1 else 0.0
    half_rel = half_db = half_deg = None
    if pp.halves and L >= 2 * pp.p:
        h2 = L // 2
        ha = tone(b[:h2], f, fs, win) / tone(a[:h2], f, fs, win)
        hb = tone(b[h2:], f, fs, win) / tone(a[h2:], f, fs, win)
        if ha and hb:
            half_rel = abs(hb / ha - 1)
            half_db = db(abs(hb) / abs(ha))
            half_deg = wrap_deg(math.degrees(np.angle(hb / ha)))

    def exc(codes: np.ndarray | None, v: np.ndarray, vpl: float) -> tuple[float, float]:
        if codes is None:
            c = 128.0 + (v - v.mean()) / vpl
        else:
            c = np.asarray(codes[:L], np.float64)
        return float(np.max(np.abs(c - c.mean()))), float(c.mean())

    e1, c1 = exc(codes1, a, vpl1)
    e2, c2 = exc(codes2, b, vpl2)
    cl1 = bool(np.any(np.asarray(clipped1)[:L])) if np.ndim(clipped1) else bool(clipped1)
    cl2 = bool(np.any(np.asarray(clipped2)[:L])) if np.ndim(clipped2) else bool(clipped2)
    return Measurement(x1, x2, n1, n2, leak, half_rel, half_db, half_deg, cl1, cl2, abs(x1) / vpl1, abs(x2) / vpl2,
                       float(a.mean()), float(b.mean()), e1, e2, c1, c2, filter_effective, fir)


# ---------------------------------------------------------------------------------------------------------
# Results


@dataclass
class PointResult:
    plan: PointPlan
    #: H as measured, after the splice (CH2 range changes) and the thru calibration
    h: complex
    #: H before the thru calibration (spliced)
    h_raw: complex
    x1: complex
    x2: complex
    ch1_range: int
    ch2_range: int
    ch1_volts_per_div: float
    ch2_volts_per_div: float
    snr_db: float
    leak_db: float
    codes1: float
    codes2: float
    filter_effective: str
    fir: bool
    flags: list[str] = field(default_factory=list)
    #: background (generator off) CH2 level in this bin, volts peak (None: not measured)
    background_v: float | None = None
    splice: complex = 1 + 0j
    retries: int = 0

    @property
    def mag_db(self) -> float:
        return db(abs(self.h))

    @property
    def phase_deg(self) -> float:
        return wrap_deg(math.degrees(np.angle(self.h)))

    @property
    def valid(self) -> bool:
        return not (set(self.flags) & BAD_FLAGS) and math.isfinite(self.mag_db)

    def row(self) -> dict[str, Any]:
        pp = self.plan
        return {"f_requested": round(pp.f_requested, 6), "f_actual": round(pp.f_actual, 6),
                "mag_db": round(self.mag_db, 4), "phase_deg": round(self.phase_deg, 3),
                "v1_vrms": round(abs(self.x1) / math.sqrt(2), 6), "v2_vrms": round(abs(self.x2) / math.sqrt(2), 6),
                "ch1_volts_per_div": self.ch1_volts_per_div, "ch2_volts_per_div": self.ch2_volts_per_div,
                "sample_rate": pp.sample_rate, "filter": self.filter_effective, "fir": self.fir,
                "snr_db": round(self.snr_db, 1) if math.isfinite(self.snr_db) else None,
                "ch2_codes": round(self.codes2, 2), "flags": list(self.flags)}


def _cross(f: np.ndarray, y: np.ndarray, level: float) -> list[float]:
    """Frequencies where y crosses level: a monotone cubic (PCHIP) through the points in log f, solved in each
    segment that brackets the level (linear in log f with fewer than three points)."""
    out: list[float] = []
    if len(y) < 2:
        return out
    x = np.log10(np.asarray(f, np.float64))
    y = np.asarray(y, np.float64)
    interp = None
    if len(y) >= 3 and np.all(np.diff(x) > 0):
        from scipy.interpolate import PchipInterpolator

        interp = PchipInterpolator(x, y - level)
    for i in range(len(y) - 1):
        a, b = y[i] - level, y[i + 1] - level
        if a == 0:
            out.append(float(f[i]))
        elif a * b < 0:
            if interp is not None:
                from scipy.optimize import brentq

                xc = brentq(lambda u: float(interp(u)), x[i], x[i + 1], xtol=1e-9)
            else:
                xc = x[i] + a / (a - b) * (x[i + 1] - x[i])
            out.append(float(10 ** xc))
    if y[-1] - level == 0:
        out.append(float(f[-1]))
    return out


def markers(points: list[PointResult]) -> dict[str, Any]:
    """Automatic markers over the valid points (design section 4): an interior peak at least 1 dB above both ends
    makes the response 'resonant' (peak, -3 dB points around it, Q = f0 / bandwidth); otherwise the reference is
    the higher end and the -3 dB crossings are reported. Phase crossings at +-45, +-90, -135, -180 deg (unwrapped)."""
    pts = [p for p in points if p.valid]
    out: dict[str, Any] = {"valid_points": len(pts), "flagged_points": len(points) - len(pts)}
    if len(pts) < 2:
        return out
    f = np.array([p.plan.f_actual for p in pts])
    m = np.array([p.mag_db for p in pts])
    ph = np.degrees(np.unwrap(np.radians([p.phase_deg for p in pts])))
    i = int(np.argmax(m))
    if 0 < i < len(m) - 1 and m[i] >= max(m[0], m[-1]) + 1.0:
        # the vertex of a parabola through the maximum and its neighbours (dB over log f)
        xl = np.log10(f[i - 1: i + 2])
        cf = np.polyfit(xl - xl[1], m[i - 1: i + 2], 2)
        xv = -cf[1] / (2 * cf[0]) if cf[0] < 0 else 0.0
        xv = float(np.clip(xv, xl[0] - xl[1], xl[2] - xl[1]))
        f_pk, ref = float(10 ** (xl[1] + xv)), float(np.polyval(cf, xv))
        ph_pk = float(np.interp(xv + xl[1], xl, ph[i - 1: i + 2]))
        lo = _cross(f[: i + 1], m[: i + 1], ref - 3.0)
        hi = _cross(f[i:], m[i:], ref - 3.0)
        pk = {"f_hz": f_pk, "mag_db": ref, "phase_deg": float(wrap_deg(ph_pk))}
        if lo and hi:
            bw = hi[0] - lo[-1]
            pk.update({"f_low_hz": lo[-1], "f_high_hz": hi[0], "bandwidth_hz": bw, "q": f_pk / bw})
        out.update({"shape": "resonant", "peak": pk, "reference_db": ref,
                    "minus_3db_hz": ([lo[-1]] if lo else []) + ([hi[0]] if hi else [])})
    else:
        ref = float(m[0] if m[0] >= m[-1] else m[-1])
        out.update({"shape": "low-pass" if m[0] > m[-1] + 3 else "high-pass" if m[-1] > m[0] + 3 else "flat",
                    "reference_db": ref, "minus_3db_hz": _cross(f, m, ref - 3.0)})
    ph_x = {}
    for target in (45.0, -45.0, 90.0, -90.0, -135.0, -180.0):
        c = _cross(f, ph, target)
        if c:
            ph_x[f"{target:+.0f}"] = c[0]
    out["phase_crossings_hz"] = ph_x
    out["max_db"] = float(m.max())
    out["min_db"] = float(m.min())
    return out


@dataclass
class SweepResult:
    points: list[PointResult]
    settle_periods: float
    ch2_mode: str
    thru_cal: str | None = None
    #: "measured" when this sweep is a thru calibration
    role: str = "measure"
    started: str = ""
    duration_s: float = 0.0
    estimate_s: float = 0.0
    range_switches: int = 0
    splices: list[dict[str, Any]] = field(default_factory=list)
    caveats: list[str] = field(default_factory=list)
    stopped_early: str | None = None
    meta: dict[str, Any] = field(default_factory=dict)

    def table(self) -> list[dict[str, Any]]:
        return [p.row() for p in self.points]

    def summary(self) -> dict[str, Any]:
        return markers(self.points)

    def write_csv(self, path: str | Path) -> str:
        rows = self.table()
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        cols = list(rows[0]) if rows else ["f_requested"]
        with open(path, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=cols, lineterminator="\n")
            w.writeheader()
            for r in rows:
                w.writerow({**r, "flags": " ".join(r["flags"])})
        return str(path)

    def to_json(self) -> dict[str, Any]:
        return {"role": self.role, "started": self.started, "duration_s": round(self.duration_s, 3),
                "estimate_s": round(self.estimate_s, 3), "settle_periods": self.settle_periods,
                "ch2_range_mode": self.ch2_mode, "thru_cal": self.thru_cal, "range_switches": self.range_switches,
                "splices": self.splices, "stopped_early": self.stopped_early, "summary": self.summary(),
                "table": self.table(), "caveats": self.caveats, "meta": self.meta}

    def write_json(self, path: str | Path) -> str:
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(self.to_json(), indent=1, default=str), encoding="utf-8")
        return str(path)

    def arrays(self) -> dict[str, np.ndarray]:
        P_ = self.points
        return {"f_requested": np.array([p.plan.f_requested for p in P_]),
                "f_actual": np.array([p.plan.f_actual for p in P_]),
                "m": np.array([p.plan.m for p in P_]), "n": np.array([p.plan.n for p in P_]),
                "h": np.array([p.h for p in P_]), "h_raw": np.array([p.h_raw for p in P_]),
                "x1": np.array([p.x1 for p in P_]), "x2": np.array([p.x2 for p in P_]),
                "mag_db": np.array([p.mag_db for p in P_]), "phase_deg": np.array([p.phase_deg for p in P_]),
                "ch1_range": np.array([p.ch1_range for p in P_]), "ch2_range": np.array([p.ch2_range for p in P_]),
                "snr_db": np.array([p.snr_db for p in P_]), "valid": np.array([p.valid for p in P_])}


# ---------------------------------------------------------------------------------------------------------
# Thru calibration


@dataclass
class ThruCal:
    """H of a sweep with both probes on the DUT input (design 2.2): the CH1 / CH2 mismatch per (M, N). Valid while
    the frequency plan, both ranges, probes and coupling stay as they were."""

    name: str
    created: str
    #: (m, n) -> H
    points: dict[tuple[int, int], complex]
    ch1_range: dict[tuple[int, int], int]
    ch2_range: dict[tuple[int, int], int]
    probes: tuple[float, float] = (1.0, 1.0)
    flags: dict[tuple[int, int], list[str]] = field(default_factory=dict)

    @classmethod
    def from_sweep(cls, res: SweepResult, name: str, probes: tuple[float, float]) -> "ThruCal":
        pts, r1, r2, fl = {}, {}, {}, {}
        for p in res.points:
            k = (p.plan.m, p.plan.n)
            pts[k] = p.h_raw
            r1[k], r2[k] = p.ch1_range, p.ch2_range
            if p.flags:
                fl[k] = list(p.flags)
        return cls(name, res.started, pts, r1, r2, probes, fl)

    def to_json(self) -> dict[str, Any]:
        return {"format": "fnirsi1013d-bode-thru", "version": 1, "name": self.name, "created": self.created,
                "probes": list(self.probes),
                "points": [{"m": k[0], "n": k[1], "re": v.real, "im": v.imag, "ch1_range": self.ch1_range[k],
                            "ch2_range": self.ch2_range[k], "flags": self.flags.get(k, [])}
                           for k, v in sorted(self.points.items(), key=lambda kv: -kv[0][0])]}

    def save(self, path: str | Path) -> str:
        path = Path(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(self.to_json(), indent=1), encoding="utf-8")
        return str(path)

    @classmethod
    def load(cls, path: str | Path) -> "ThruCal":
        d = json.loads(Path(path).read_text(encoding="utf-8"))
        if d.get("format") != "fnirsi1013d-bode-thru":
            raise ValueError(f"{path}: not a thru calibration file")
        pts, r1, r2, fl = {}, {}, {}, {}
        for e in d["points"]:
            k = (int(e["m"]), int(e["n"]))
            pts[k] = complex(e["re"], e["im"])
            r1[k], r2[k] = int(e["ch1_range"]), int(e["ch2_range"])
            if e.get("flags"):
                fl[k] = list(e["flags"])
        return cls(d["name"], d.get("created", ""), pts, r1, r2, tuple(d.get("probes", (1.0, 1.0))), fl)

    def apply(self, p: PointResult) -> None:
        """Divide the point's H by the calibration's H at the same (M, N); flags 'no_cal' when the plan has no such
        point and 'cal_range' when the ranges differ from the calibration's."""
        k = (p.plan.m, p.plan.n)
        hc = self.points.get(k)
        if hc is None or not hc or not math.isfinite(abs(hc)):
            p.flags.append("no_cal")
            return
        p.h = p.h_raw / hc
        if self.ch1_range[k] != p.ch1_range or self.ch2_range[k] != p.ch2_range:
            p.flags.append("cal_range")
        if set(self.flags.get(k, [])) & BAD_FLAGS:
            p.flags.append("cal_flagged")


__all__ = ["BodeError", "Measurement", "PointPlan", "PointResult", "SweepResult", "ThruCal", "CONNECTION_TEXT",
           "SETTLE_PRESETS", "estimate_duration", "markers", "measure_pair", "parse_settle", "plan_point", "plan_sweep",
           "plan_vectors", "residual_noise", "tone"]
