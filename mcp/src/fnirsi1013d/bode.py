"""Host-driven Bode / frequency-response sweep (docs/plans/bode-design.md steps H1-H3).

Stepped single tone: the calibration pin's generator (since FPGA 0x153D the two-step square on the 5 ns clock,
docs/plans/fpga-0x153c.md 7.4) runs a strictly periodic square of an integer period P (n_h = ceil(P / 2), n_l =
floor(P / 2), no fraction), so f = 200 MHz / P exactly; CH1 sits on the DUT input, CH2 on its output, and H(f) =
X2 / X1 from a single-bin DFT at f over a coherent record (an integer number of stimulus periods), so the square
wave's harmonics land on other bins and drop out. Up to protocol 1.26 the stimulus was the 50 MHz DDS with M = P / 4
(the plan maps exactly with P = 4 M; the grid is four times finer now).

Rate per point (design 3.2): the 0x1535 CIC3 + FIR at
N = 16 x 2^k (FIR-legal: N >= 16, N % 4 == 0) with 257..512 samples per period below 24 kHz, N = 16 (12.5 MSa/s) up to
1.5 MHz, legacy 200 MSa/s above.

Pieces, from pure to hardware:

- ``plan_point`` / ``plan_sweep``: the deterministic frequency plan (P, N, samples per period p / q, record L,
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

import argparse
import csv
import datetime as _dt
import json
import math
import sys
import time
from dataclasses import asdict, dataclass, field
from fractions import Fraction
from pathlib import Path
from typing import Any, Callable, Iterable

import numpy as np

from . import protocol as P

GEN_CLOCK_HZ = P.GEN_CLOCK_HZ  # the generator's clock: 200 MHz (5 ns)
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
#: coherence error (periods) above which the DFT uses a Hann window; with an integer period P the record is exactly
#: coherent (the DDS's 32-bit increment rounding needed it below about 50 Hz up to protocol 1.26)
HANN_ABOVE_PERIODS = 1e-3
#: the shortest stimulus period in 5 ns clocks (2 + 2: 50 MHz; the plan stops at MAX_FREQ_HZ anyway)
MIN_PERIOD = 4
#: settle presets in stimulus periods before the record (design 3.3)
SETTLE_PRESETS = {"fast": 3, "normal": 10, "resonant": 50, "high_q": 200}
FIRST_POINT_SETTLE_FACTOR = 3
MIN_SETTLE_S = 1e-3
#: host overhead per point (SET_GEN, ARM, WAIT, READ_RAW of 2 x 4096 16-bit samples, Python), design 3.7
HOST_OVERHEAD_S = 0.04
RELAY_SETTLE_S = 0.05

# quality gates (design 3.3, 3.5, 3.6, section 4 warnings)
CH1_LOW_CODES = 20.0  # CH1 fundamental below this (codes peak): "ch1_low"
#: CH1 fundamental below this (codes peak) or a CH1 record flatter than FLAT_STD_CODES: no stimulus to divide by;
#: the point is retaken once, then flagged ch1_low + incoherent with H = NaN
CH1_MIN_CODES = 0.5
#: a record whose standard deviation is under this (codes) holds no tone worth measuring: a stuck or saturated ADC
#: record (bench 2026-10-20: the first decimated record after a rate / range change on FPGA 0x153A was one saturated
#: value), retaken once
FLAT_STD_CODES = 1.0
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

#: flags that make a point unusable for markers (drawn grey by the on-device view and in the report plot). fw 0.2.99
#: (owner 2026-10-20 16:22): ch2_small too: CH2 under 3 codes is below what the ADC measures (bench: a two-pole RC's
#: tail above 500 kHz was generator pickup at under one code, drawn as data)
BAD_FLAGS = frozenset({"ch1_clipped", "clipped", "ch2_overload", "ch2_small", "unsettled", "incoherent", "near_floor",
                       "low_snr"})
#: CH2's finest hardware range (100 mV/div): a ch2_small point there is under the floor, no range helps
CH2_FINE_RANGE = 5

#: wiring the person must use (design 2.5); shown with the first sweep of a session
CONNECTION_TEXT = (
    "Wiring: calibration post -> DUT input, CH1 probe tip on the DUT input; CH2 probe tip on the DUT output. "
    "Source: the post is a 3.1 Vpp square through 1 kOhm (R21); H = CH2 / CH1, so CH1 must touch the DUT input. "
    "Ground (default, wiring A): the DUT ground on the probe ground clips. Bench 2026-10-19: wiring A and the USB-ground "
    "wiring gave the same H within 0.4 dB from 1 kHz to 300 kHz on a 1 kOhm / 15 nF RC; the feared return error through "
    "R71 did not show. Alternative (wiring B, for a DUT that must not see the probe reference): the DUT ground on the "
    "scope's USB ground (USB shell), both clips OPEN; the probes then read the DUT 1.24 V below their reference, which "
    "saturates CH2 at 0.1 and 0.2 V/div with DC coupling (use ch2_coupling='ac' there). Never both (a clip on a node "
    "tied to USB ground loads VRF1 and shifts every trace). Passive DUTs or buffered inputs only: the post has no "
    "protection beyond the 1 kOhm (never a coil, a MOSFET drain or anything that can drive the post).")

#: the CH2 record is this flat (largest distance from its mean, codes) while its tone is small: the front end in front
#: of the offset injection is pinned (a DC level beyond the range's input window), the ADC sees a constant
FLAT_CODES = 0.05
#: after a CH2 range change at the same frequency the tone (volts) fell below this fraction of what the previous range
#: measured: the new range's front end is saturated (its tone is gone, whatever noise the ADC adds)
COLLAPSE_RATIO = 0.1
OVERLOAD_TEXT = ("CH2's front end is saturated (a flat record on {vpd} V/div: a DC level of about {vdc:+.2f} V against "
                 "the probe reference is beyond this range's input window). Remedy: wiring A (DUT ground on the probe "
                 "ground clips, about 0 V DC), or ch2_coupling='ac', or a less sensitive CH2 range.")


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


def period_frequency(period: int) -> float:
    """The stimulus frequency of a period of P clocks of 5 ns: 200 MHz / P, exact."""
    return GEN_CLOCK_HZ / period


def period_mhz(period: int) -> int:
    """200e9 / P in mHz, rounded half to even in integers (the firmware's planner, bode_dsp.c)."""
    q, r = divmod(int(GEN_CLOCK_HZ) * 1000, period)
    return q + (1 if 2 * r > period or (2 * r == period and q & 1) else 0)


@dataclass(frozen=True)
class PointPlan:
    """One frequency point (design 3.2)."""

    f_requested: float
    #: stimulus period P in 5 ns clocks (SET_GEN P16 = P << 16, H16 = ceil(P / 2) << 16)
    period: int
    #: decimation factor (1 = legacy 200 MSa/s)
    n: int
    #: the frequency the DFT uses: 200 MHz / P, exact
    f_actual: float
    #: samples per period P / N = p / q in lowest terms
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
        """Stimulus periods in the record (exact: the period is an integer number of clocks)."""
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
        """Periods by which the record misses a whole number at the stimulus frequency: 0 up to the float rounding
        (an integer period P; the DDS of protocol 1.26 and older missed by up to 0.5 % at 1 Hz)."""
        return abs(self.record * self.f_actual / self.sample_rate - self.periods)

    @property
    def window(self) -> str:
        """'rect' for a coherent record (always, on the integer-period stimulus); 'hann' would be used more than 1e-3
        periods off."""
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
    p0 = max(MIN_PERIOD, int(round(GEN_CLOCK_HZ / f_req)))
    if GEN_CLOCK_HZ / p0 > LEGACY_ABOVE_HZ:
        n, period = 1, p0
    else:
        n = 16
        while p0 > MAX_SPP * n:
            n *= 2
        # below 24 kHz the samples per period must be an integer S (else p keeps P's odd part and no record fits)
        period = p0 if n == 16 else int(round(p0 / n)) * n
        if n == 16 and period > MAX_RECORD // 2:
            # an odd P above 4095 at N = 16 has q = 16 and one block of P samples per record (no halves, no noise
            # estimate): round P to even (the C planner's rule, bode_dsp.c div_round_even)
            period = int(round(p0 / 2)) * 2
    spp = Fraction(period, n)
    p, q = spp.numerator, spp.denominator
    j = max(math.ceil(MIN_PERIODS / q), math.ceil(MIN_RECORD / p))
    if j % 2 and (j + 1) * p <= MAX_RECORD:
        j += 1  # an even number of blocks: the halves hold whole periods (settle check)
    if j * p > MAX_RECORD:
        raise ValueError(f"no coherent record for {f_req:g} Hz (P {period}, N {n}, p {p})")  # cannot happen in range
    return PointPlan(f_req, period, n, period_frequency(period), p, q, j * p, j)


def log_points(start: float, stop: float, points: int) -> list[float]:
    if points < 1:
        raise ValueError("points must be >= 1")
    if start <= 0 or stop <= 0:
        raise ValueError("start and stop must be > 0")
    if points == 1:
        return [float(start)]
    return [float(x) for x in np.geomspace(start, stop, points)]


def plan_sweep(start: float, stop: float, points: int) -> list[PointPlan]:
    """Log-spaced points from start to stop (either order; measured low to high), snapped to an integer period P,
    merged when two requests give the same P (design 3.1)."""
    lo, hi = sorted((float(start), float(stop)))
    out: list[PointPlan] = []
    for f in log_points(lo, hi, points):
        pp = plan_point(f)
        if out and out[-1].period == pp.period:
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
        # f_req to 9 digits: np.geomspace differs in the last bit between platforms; the plan does not
        return {"f_req": float(f"{pp.f_requested:.9g}"), "period": pp.period, "n": pp.n, "divider": pp.divider,
                "f_mhz": period_mhz(pp.period), "p": pp.p, "q": pp.q,
                "record": pp.record, "total": pp.total_samples}

    def sweep(a: float, b: float, n: int) -> dict[str, Any]:
        return {"start": a, "stop": b, "points": n, "plan": [row(pp) for pp in plan_sweep(a, b, n)]}

    return {"format": "fnirsi1013d-bode-plan", "version": 2,
            "notes": "mcp/src/fnirsi1013d/bode.py plan_point / plan_sweep (docs/plans/bode-design.md 3.1-3.2). "
                     "f_req in Hz to 9 significant digits (sweeps: np.geomspace), period = P in 5 ns clocks (FPGA "
                     "0x153D square), f_mhz = round(200e9 / P), divider = N - 1 (0 = legacy 200 MSa/s), record = "
                     "samples analysed, total = the even trigger total. "
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
    if math.isnan(x):
        return math.nan
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
    #: set by the runner: CH2's tone vanished after a range change at the same frequency (COLLAPSE_RATIO)
    collapsed: bool = False
    #: standard deviation of the records in codes (inf: not computed)
    std1: float = math.inf
    std2: float = math.inf
    #: set by the runner: this is the retake of a flat / dead record (``BodeRunner._measure``)
    retaken: bool = False

    @property
    def flat1(self) -> bool:
        """CH1's record is (nearly) constant: a stuck or saturated ADC record, or no stimulus at all."""
        return self.std1 < FLAT_STD_CODES

    @property
    def ch1_dead(self) -> bool:
        """No usable CH1 tone to divide by (H is NaN)."""
        return self.flat1 or not self.codes1 >= CH1_MIN_CODES

    @property
    def suspect(self) -> bool:
        """Worth one retake: CH1 flat or without a tone, or CH2 constant (the 0x153A first-record artefact looks
        like either; a real CH2 overload stays flat on the retake)."""
        return self.ch1_dead or self.std2 < FLAT_STD_CODES and self.ac2 < FLAT_CODES

    @property
    def flat2(self) -> bool:
        """CH2 saturated: a constant record with no tone, or a tone that vanished on this range (the front end in
        front of the offset injection pinned by a DC level beyond the range's window). Not while CH1 is flat too:
        both records constant is a stuck readout (the 0x153A first-record artefact), not a CH2 overload."""
        if self.flat1:
            return False
        return self.collapsed or (self.ac2 < FLAT_CODES and self.codes2 < CH2_SMALL_CODES)

    @property
    def h(self) -> complex:
        return complex("nan") if self.ch1_dead or not self.x1 else self.x2 / self.x1

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
        ta, tb = tone(a[:h2], f, fs, win), tone(a[h2:], f, fs, win)
        floor = CH1_MIN_CODES * vpl1  # a flat or dead CH1 half has nothing to divide by (bench 2026-10-20)
        ha = tone(b[:h2], f, fs, win) / ta if abs(ta) >= floor else 0j
        hb = tone(b[h2:], f, fs, win) / tb if abs(tb) >= floor else 0j
        if ha and hb:
            half_rel = abs(hb / ha - 1)
            half_db = db(abs(hb) / abs(ha))
            half_deg = wrap_deg(math.degrees(np.angle(hb / ha)))

    def exc(codes: np.ndarray | None, v: np.ndarray, vpl: float) -> tuple[float, float, float]:
        if codes is None:
            c = 128.0 + (v - v.mean()) / vpl
        else:
            c = np.asarray(codes[:L], np.float64)
        return float(np.max(np.abs(c - c.mean()))), float(c.mean()), float(c.std())

    e1, c1, sd1 = exc(codes1, a, vpl1)
    e2, c2, sd2 = exc(codes2, b, vpl2)
    cl1 = bool(np.any(np.asarray(clipped1)[:L])) if np.ndim(clipped1) else bool(clipped1)
    cl2 = bool(np.any(np.asarray(clipped2)[:L])) if np.ndim(clipped2) else bool(clipped2)
    return Measurement(x1, x2, n1, n2, leak, half_rel, half_db, half_deg, cl1, cl2, abs(x1) / vpl1, abs(x2) / vpl2,
                       float(a.mean()), float(b.mean()), e1, e2, c1, c2, filter_effective, fir, std1=sd1, std2=sd2)


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
        mag, ph = self.mag_db, self.phase_deg
        return {"f_requested": round(pp.f_requested, 6), "f_actual": round(pp.f_actual, 6),
                "mag_db": round(mag, 4) if math.isfinite(mag) else None,
                "phase_deg": round(ph, 3) if math.isfinite(ph) else None,
                "v1_vrms": round(abs(self.x1) / math.sqrt(2), 6), "v2_vrms": round(abs(self.x2) / math.sqrt(2), 6),
                "ch1_volts_per_div": self.ch1_volts_per_div, "ch2_volts_per_div": self.ch2_volts_per_div,
                "sample_rate": pp.sample_rate, "filter": self.filter_effective, "fir": self.fir,
                "snr_db": round(self.snr_db, 1) if math.isfinite(self.snr_db) else None,
                "ch2_codes": round(self.codes2, 2) if math.isfinite(self.codes2) else None,
                "leak_db": round(self.leak_db, 1) if math.isfinite(self.leak_db) else None, "flags": list(self.flags)}


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
                "period": np.array([p.plan.period for p in P_]), "n": np.array([p.plan.n for p in P_]),
                "h": np.array([p.h for p in P_]), "h_raw": np.array([p.h_raw for p in P_]),
                "x1": np.array([p.x1 for p in P_]), "x2": np.array([p.x2 for p in P_]),
                "mag_db": np.array([p.mag_db for p in P_]), "phase_deg": np.array([p.phase_deg for p in P_]),
                "ch1_range": np.array([p.ch1_range for p in P_]), "ch2_range": np.array([p.ch2_range for p in P_]),
                "snr_db": np.array([p.snr_db for p in P_]), "valid": np.array([p.valid for p in P_])}


# ---------------------------------------------------------------------------------------------------------
# Thru calibration


@dataclass
class ThruCal:
    """H of a sweep with both probes on the DUT input (design 2.2): the CH1 / CH2 mismatch per (P, N). Valid while
    the frequency plan, both ranges, probes and coupling stay as they were. File version 2 (protocol 1.27, keys by
    the period P); a version 1 file (the DDS's M) is refused: measure the thru again."""

    name: str
    created: str
    #: (period, n) -> H
    points: dict[tuple[int, int], complex]
    ch1_range: dict[tuple[int, int], int]
    ch2_range: dict[tuple[int, int], int]
    probes: tuple[float, float] = (1.0, 1.0)
    flags: dict[tuple[int, int], list[str]] = field(default_factory=dict)

    @classmethod
    def from_sweep(cls, res: SweepResult, name: str, probes: tuple[float, float]) -> "ThruCal":
        pts, r1, r2, fl = {}, {}, {}, {}
        for p in res.points:
            k = (p.plan.period, p.plan.n)
            pts[k] = p.h_raw
            r1[k], r2[k] = p.ch1_range, p.ch2_range
            if p.flags:
                fl[k] = list(p.flags)
        return cls(name, res.started, pts, r1, r2, probes, fl)

    def to_json(self) -> dict[str, Any]:
        return {"format": "fnirsi1013d-bode-thru", "version": 2, "name": self.name, "created": self.created,
                "probes": list(self.probes),
                "points": [{"period": k[0], "n": k[1], "re": v.real, "im": v.imag, "ch1_range": self.ch1_range[k],
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
        if d.get("version", 1) < 2:
            raise ValueError(f"{path}: a thru calibration of the DDS stimulus (protocol 1.26 and older, keyed by M): "
                             "the generator is the 5 ns square since FPGA 0x153D; measure the thru again")
        pts, r1, r2, fl = {}, {}, {}, {}
        for e in d["points"]:
            k = (int(e["period"]), int(e["n"]))
            pts[k] = complex(e["re"], e["im"])
            r1[k], r2[k] = int(e["ch1_range"]), int(e["ch2_range"])
            if e.get("flags"):
                fl[k] = list(e["flags"])
        return cls(d["name"], d.get("created", ""), pts, r1, r2, tuple(d.get("probes", (1.0, 1.0))), fl)

    def apply(self, p: PointResult) -> None:
        """Divide the point's H by the calibration's H at the same (P, N); flags 'no_cal' when the plan has no such
        point and 'cal_range' when the ranges differ from the calibration's."""
        k = (p.plan.period, p.plan.n)
        hc = self.points.get(k)
        if hc is None or not hc or not math.isfinite(abs(hc)):
            p.flags.append("no_cal")
            return
        p.h = p.h_raw / hc
        if self.ch1_range[k] != p.ch1_range or self.ch2_range[k] != p.ch2_range:
            p.flags.append("cal_range")
        if set(self.flags.get(k, [])) & BAD_FLAGS:
            p.flags.append("cal_flagged")


def _usable(h: complex) -> bool:
    return bool(h) and math.isfinite(abs(h))


# ---------------------------------------------------------------------------------------------------------
# Hardware runner


@dataclass
class _Saved:
    channels: list[P.ChannelConfig]
    timebase: P.Timebase
    trigger: P.Trigger
    gen: P.Gen
    decim: P.DecimState | None
    run: bool


class BodeRunner:
    """Runs sweeps on a ``Scope`` (collaborative or legacy firmware; the caller takes control first). One instance
    per sweep. ``sleep`` and ``relay_wait`` (called before every range change, e.g. the MCP server's relay rate
    limit) are injectable for tests; ``progress(done, total, text)`` is called after every point."""

    def __init__(self, scope: Any, *, settle: str | float = "normal", ch2_range: str | float | int = "auto",
                 background: bool = True, max_switches: int = MAX_RANGE_SWITCHES, splice: bool = True,
                 sleep: Callable[[float], None] = time.sleep, relay_wait: Callable[[], Any] | None = None,
                 progress: Callable[[int, int, str], None] | None = None, busy_retry_s: float = 1.0,
                 ch2_coupling: str = "dc"):
        self.s = scope
        if ch2_coupling not in ("dc", "ac"):
            raise ValueError(f"ch2_coupling {ch2_coupling!r}: 'dc' or 'ac'")
        #: CH2 coupling: 'ac' removes the DUT's DC (wiring B) at the price of the coupling capacitor below ~10 Hz
        self.ch2_coupling = ch2_coupling
        #: channels whose offset reached the PWM limit while re-centring (reported once)
        self._offset_exhausted: set[int] = set()
        self.settle_periods = parse_settle(settle)
        self.ch2_mode, self.ch2_vpd = self._parse_ch2(ch2_range)
        self.background = background
        self.max_switches = max_switches
        self.splice_on = splice
        self.sleep = sleep
        self.relay_wait = relay_wait
        self.progress = progress
        self.busy_retry_s = busy_retry_s
        self.range = [0, 0]
        self.switches = 0
        self.k_splice = 1 + 0j
        self.splices: list[dict[str, Any]] = []
        self.caveats: list[str] = []
        self._bg: dict[tuple[int, int], np.ndarray] = {}
        self._cur_n: int | None = None
        self._cur_total: int | None = None
        self._dcsv = [1000, 1000]
        self._gen_on = False
        self._user_ch2_range = 3
        #: CH2 ranges a down-range step clipped on (the prediction failed there): not tried again
        self._bad_down: set[int] = set()
        self._v_dc2 = 0.0
        #: the prescan's range-1 measurement and its frequency (the first point re-checks CH2 against it)
        self._ref2: tuple[float, Measurement] | None = None
        #: the generator went off or on: the DUT's DC state changed, the next point gets the first-point settle
        self._disturbed = True
        #: records retaken because they were flat / had no CH1 tone, and the frequencies still flat after the retake
        self.flat_retakes = 0
        self._flat_twice: list[float] = []

    @staticmethod
    def _parse_ch2(v: str | float | int) -> tuple[str, float | None]:
        """'auto', 'fixed' (CH2's current range) or a V/div at the probe tip (the nearest hardware range)."""
        if isinstance(v, str):
            s = v.strip().lower()
            if s in ("auto", "fixed"):
                return s, None
            from .units import parse_quantity

            vpd = parse_quantity(s, "V")
        else:
            vpd = float(v)
        if not vpd > 0:
            raise ValueError(f"ch2_range {v!r}: use 'auto', 'fixed' or a V/div > 0")
        return "fixed", vpd

    def _fixed_ch2_range(self) -> int:
        if self.ch2_vpd is None:
            return self._user_ch2_range
        probe = self.s.get_channel(1).probe_factor
        return int(np.argmin([abs(math.log(self.ch2_vpd / (x * probe))) for x in P.RANGE_VOLTS_PER_DIV[:6]]))

    # --- settings -------------------------------------------------------------------------------------------

    def _save(self) -> _Saved:
        s = self.s
        st = s.status()
        dec = s.get_decim() if s.info.caps & P.Caps.DECIM else None
        return _Saved([s.get_channel(0, cached=False), s.get_channel(1, cached=False)], s.get_timebase(cached=False),
                      s.get_trigger(cached=False), s.get_gen(), dec,
                      bool(s.shared and st.run_state == P.RunState.RUN))

    def _restore(self, sv: _Saved) -> list[str]:
        s, errs = self.s, []

        def step(what: str, fn: Callable[[], Any]) -> None:
            try:
                fn()
            except Exception as e:  # noqa: BLE001 - restore as much as possible, report the rest
                errs.append(f"{what}: {e}")

        def gen() -> None:
            note = s.restore_gen(sv.gen)
            if note:
                errs.append(f"generator: {note}")
        step("generator", gen)
        if sv.decim is not None:
            step("filter", lambda: s.set_decim(sv.decim.filter, sv.decim.flags))
        for c, cfg in enumerate(sv.channels):
            if cfg.range != self.range[c]:
                self._relay()
            step(f"CH{c + 1}", lambda c=c, cfg=cfg: s.set_channel(
                c, enable=bool(cfg.enable), range_index=cfg.range, coupling=cfg.coupling, dcoffset=cfg.dcoffset,
                invert=bool(cfg.invert), probe_index=cfg.probe_index))
        tb = sv.timebase
        if tb.timeperdiv_index != 0xFF:
            step("time base", lambda: s.set_timebase(ui_index=tb.timeperdiv_index))
        else:
            step("time base", lambda: s.set_timebase(divider=tb.divider))
        tr = sv.trigger
        step("trigger", lambda: s.set_trigger(source=tr.source, edge=tr.edge, level=tr.level, mode=tr.mode,
                                              pretrigger_samples=tr.pretrigger_samples,
                                              total_samples=tr.total_samples, holdoff=tr.holdoff or None))
        if sv.run:
            step("RUN", lambda: s.set_acq_state(True))
        return errs

    def _relay(self) -> None:
        if self.relay_wait is not None:
            self.relay_wait()

    def _set_range(self, c: int, r: int, v_dc: float | None) -> None:
        """Switch channel c to hardware range r and centre its offset on the DC level v_dc (volts at the tip)."""
        s = self.s
        if r != self.range[c]:
            self._relay()
        cfg = s.set_channel(c, range_index=r, dcoffset=0)
        self.range[c] = r
        if v_dc is not None:
            self._centre_on(c, cfg, v_dc)
        self.sleep(RELAY_SETTLE_S)

    def _centre_on(self, c: int, cfg: P.ChannelConfig, v_dc: float) -> None:
        """dcoffset so that v_dc sits at code 128: zero code = 128 - dcoffset x 100 / dc_shift_value."""
        vpl = self.s.scale(c, cfg).volts_per_lsb
        codes = v_dc / vpl
        self._shift_offset(c, cfg, codes)

    def _shift_offset(self, c: int, cfg: P.ChannelConfig, codes: float) -> bool:
        """Move the trace down by ``codes`` (positive = the record's mean is that far above 128). False when the offset
        PWM has no room left (clamped at 0 / 2000, or SET_CHANNEL refused it): noted once as a caveat."""
        from .transport import DeviceError

        want = cfg.dcoffset + int(round(codes * self._dcsv[c] / 100.0))
        lo, hi = -cfg.center_cal, 2000 - cfg.center_cal
        d = max(lo, min(hi, want))
        ok = d == want
        if d != cfg.dcoffset:
            try:
                self.s.set_channel(c, dcoffset=d)
            except DeviceError as e:
                if e.status != P.Status.E_BAD_ARG:
                    raise
                ok = False
            else:
                self.sleep(RELAY_SETTLE_S)  # the offset PWM's filter in the front end
        if not ok and c not in self._offset_exhausted:
            self._offset_exhausted.add(c)
            self.caveats.append(f"CH{c + 1}: offset range exhausted (the trace could not be centred within the "
                                "offset PWM's 0..2000): the DC level is beyond this range's window; see the "
                                "ch2_overload / clipped flags")
        return ok

    def _prepare(self, plans: list[PointPlan]) -> None:
        s = self.s
        caps = s.info.caps
        if not caps & P.Caps.DECIM:
            raise BodeError("the Bode sweep needs the decimation filter (FPGA 0x1534+, caps DECIM): without it the "
                            "slower rates skip samples and alias the stimulus harmonics")
        if not caps & P.Caps.GENERATOR:
            raise BodeError("the Bode sweep needs the programmable generator (AL3 bitstream)")
        if caps & P.Caps.EXT_TRIGGER and s.get_trigger(cached=False).source == 2:
            self.caveats.append("the trigger source was EXT (the calibration pin): set to CH1 for the sweep and "
                                "restored afterwards (which switches the generator off again)")
        for c in (0, 1):
            self._dcsv[c] = s.get_cal(c).dc_shift_value or 1000
        filt = "cic3+fir" if caps & P.Caps.FIR else "cic3"
        if not caps & P.Caps.FIR:
            self.caveats.append("no compensation FIR on this bitstream (0x1534): CIC3 only; the droop cancels in "
                                "the ratio but the alias rejection is lower")
        # filter first with a short total (SET_DECIM refuses a filter while the total is over 8190)
        s.set_trigger(source=0, edge=0, level=128, mode=0, pretrigger_samples=0,
                      total_samples=plans[0].total_samples)
        s.set_decim(filt)
        for c in (0, 1):
            s.set_channel(c, enable=True, coupling=1 if c == 1 and self.ch2_coupling == "ac" else 0)
        if self.ch2_coupling == "ac":
            self.caveats.append("CH2 AC coupled: the coupling capacitor (about 1 Hz corner) adds magnitude and phase "
                                "error below about 10 Hz; take the thru calibration with the same coupling")

    def _set_rate(self, pp: PointPlan) -> None:
        s = self.s
        if pp.n != self._cur_n:
            s.set_timebase(divider=pp.divider)
            self._cur_n = pp.n
        if pp.total_samples != self._cur_total:
            s.set_trigger(pretrigger_samples=0, total_samples=pp.total_samples)
            self._cur_total = pp.total_samples

    def _gen(self, on: bool, pp: PointPlan | None = None) -> None:
        if pp is not None:
            p16, h16 = P.gen_period_p16(pp.period)
            self.s.set_gen(enable=on, p16=p16, h16=h16)
        else:
            self.s.set_gen(enable=on)
        if on != self._gen_on:
            self._disturbed = True
        self._gen_on = on

    def _capture(self, pp: PointPlan) -> Any:
        from .transport import DeviceError

        tries = 0
        while True:
            try:
                cap = self.s.capture(["CH1", "CH2"], timeout_s=2 * pp.record_s + 1.0)
            except DeviceError as e:
                if e.status == P.Status.E_BUSY and tries == 0:
                    tries += 1
                    self.sleep(self.busy_retry_s)
                    continue
                raise
            if cap is None:
                raise BodeError(f"no capture at {pp.f_actual:.6g} Hz (no trigger and no auto time-out)")
            return cap

    def _measure(self, pp: PointPlan, settle_periods: float) -> Measurement:
        """One analysed capture. A flat record or one without a CH1 tone (``Measurement.suspect``: the 0x153A
        first-record artefact, a stuck readout) is retaken once; a CH1 still dead after that is recorded for the
        caveat (``_flat_twice``) and its point carries ch1_low + incoherent and H = NaN, never a traceback."""
        ms = self._measure_once(pp, settle_periods)
        if not ms.suspect:
            return ms
        self.flat_retakes += 1
        ms = self._measure_once(pp, settle_periods)
        ms.retaken = True
        if ms.ch1_dead and pp.f_actual not in self._flat_twice:
            self._flat_twice.append(pp.f_actual)
        return ms

    def _measure_once(self, pp: PointPlan, settle_periods: float) -> Measurement:
        self.sleep(pp.settle_s(settle_periods) + pp.filter_settle_s())
        cap = self._capture(pp)
        c1, c2 = cap.channels["CH1"], cap.channels["CH2"]
        dec = cap.decim
        filt = "legacy"
        if dec is not None and dec.k:
            filt = P.DECIM_FILTERS[dec.k] + ("+fir" if dec.fir else "")
        return measure_pair(c1.volts, c2.volts, pp, vpl1=c1.volts_per_lsb, vpl2=c2.volts_per_lsb,
                            clipped1=c1.clipped, clipped2=c2.clipped, codes1=c1.codes, codes2=c2.codes,
                            filter_effective=filt, fir=bool(dec is not None and dec.fir))

    def _point_settle(self) -> float:
        k = FIRST_POINT_SETTLE_FACTOR if self._disturbed else 1
        self._disturbed = False
        return self.settle_periods * k

    def _recentre_if_needed(self, ms: Measurement) -> bool:
        """Re-centre a channel whose record mean is far from code 128 (the offset formula's slope differs from the
        real PWM slope on some units). True when something moved."""
        moved = False
        for c, centre in ((0, ms.centre1), (1, ms.centre2)):
            if abs(centre - 128.0) > CENTRE_TOLERANCE_CODES and c not in self._offset_exhausted:
                cfg = self.s.get_channel(c, cached=False)
                if self._shift_offset(c, cfg, centre - 128.0):
                    moved = True
        return moved

    def _prescan(self, pp: PointPlan) -> None:
        """Ranges from a capture at the first frequency on range 1 (+-11 V at x1): CH1 the most sensitive range that
        keeps the peak inside 85 % of full scale, CH2 the same in auto mode or the fixed range; both centred."""
        s = self.s
        for c in (0, 1):
            self._set_range(c, 1, 0.0)
        self._set_rate(pp)
        self._gen(True, pp)
        ms = self._measure(pp, self.settle_periods * FIRST_POINT_SETTLE_FACTOR)
        self._disturbed = False
        ref = ms  # range 1: the reference tone for the overload check
        self._ref2 = (pp.f_actual, ms)
        v_dc = (ms.mean1, ms.mean2)
        self._v_dc2 = ms.mean2  # on range 1: CH2's DC against the probe reference (a flat record has no usable mean)
        targets = []
        for c in (0, 1):
            cfg = s.get_channel(c)
            vpl = s.scale(c, cfg).volts_per_lsb
            v_pk = max((ms.ac1, ms.ac2)[c], 1.0) * vpl  # peak distance from the record mean, volts
            if c == 1 and self.ch2_mode == "fixed":
                r = self._fixed_ch2_range()
            elif c == 0 and ms.ch1_dead:
                r = 1  # nothing to range on (flat twice or no tone): stay on the pre-scan's range, caveat below
                self.caveats.append(f"the pre-scan's CH1 record at {pp.f_actual:.6g} Hz was flat or had no tone twice "
                                    "(a stuck or saturated ADC record, or no stimulus on CH1): CH1 stays on 2.5 V/div "
                                    "x probe; check the CH1 probe on the DUT input and scope_health")
            else:
                r = 1
                for cand in range(1, 6):
                    vpl_c = vpl * P.RANGE_VOLTS_PER_DIV[cand] / P.RANGE_VOLTS_PER_DIV[1]
                    if v_pk / vpl_c <= FULL_SCALE_FRACTION * 128:
                        r = cand
            targets.append(r)
        for c in (0, 1):
            self._set_range(c, targets[c], v_dc[c])
        # verify: a clip steps back one range, an off-centre record re-centres
        for _ in range(4):
            ms = self._measure(pp, self.settle_periods)
            if ms.ch1_dead:
                break  # flat even after the retake: nothing to range on, keep the ranges
            changed = False
            over = self.range[1] != 1 and self._check(ref, ms).flat2
            if over and self.ch2_mode == "fixed":
                raise BodeError(self._overload_text(v_dc[1]))
            for c, clipped in ((0, ms.clipped1), (1, ms.clipped2 or over)):
                if clipped and self.range[c] > 0 and not (c == 1 and self.ch2_mode == "fixed"):
                    if c == 1 and over:
                        self._bad_down.add(self.range[1])
                    self._set_range(c, self.range[c] - 1, (ms.mean1, v_dc[1] if over else ms.mean2)[c])
                    changed = True
            if not changed:
                changed = self._recentre_if_needed(ms)
            if not changed:
                break

    @staticmethod
    def _collapsed(ref: Measurement, new: Measurement) -> bool:
        """CH2's tone vanished on the new range (same frequency): ``ref`` measured it clearly on the previous one."""
        return ref.codes2 >= 1.0 and not ref.clipped2 and abs(new.x2) < COLLAPSE_RATIO * abs(ref.x2)

    def _check(self, ref: Measurement | None, new: Measurement) -> Measurement:
        """Mark ``new`` collapsed against ``ref`` (the same frequency on another CH2 range)."""
        if ref is not None and self._collapsed(ref, new):
            new.collapsed = True
        return new

    def _ref_check(self, pp: PointPlan, ms: Measurement) -> Measurement:
        """At the prescan's frequency on another CH2 range: compare with the range-1 tone; fixed mode stops on a
        saturated CH2 (no point of the sweep would be valid)."""
        if self._ref2 is not None and pp.f_actual == self._ref2[0] and self.range[1] != 1:
            self._check(self._ref2[1], ms)
        if ms.flat2 and self.ch2_mode == "fixed":
            raise BodeError(self._overload_text(self._v_dc2))
        return ms

    def _overload_text(self, v_dc: float) -> str:
        vpd = P.RANGE_VOLTS_PER_DIV[self.range[1]] * self.s.get_channel(1).probe_factor
        return OVERLOAD_TEXT.format(vpd=f"{vpd:g}", vdc=v_dc)

    def _background(self, pp: PointPlan, plans: list[PointPlan]) -> None:
        """Generator off, one capture at this rate and CH2 range: the CH2 level in every bin the sweep will use at
        this rate (offset-PWM ripple, hum, pickup). Recorded per (N, CH2 range)."""
        key = (pp.n, self.range[1])
        if not self.background or key in self._bg:
            return
        group = [q for q in plans if q.n == pp.n]
        rec = max(q.total_samples for q in group)
        self._gen(False)
        if rec != self._cur_total:
            self.s.set_trigger(pretrigger_samples=0, total_samples=rec)
            self._cur_total = rec
        self.sleep(pp.filter_settle_s() + MIN_SETTLE_S)
        v = np.asarray(self._capture(pp).channels["CH2"].volts, np.float64)
        if not np.ptp(v):  # a constant record (the 0x153A first-record artefact) has no background level: once more
            self.flat_retakes += 1
            v = np.asarray(self._capture(pp).channels["CH2"].volts, np.float64)
        self._bg[key] = v

    def _bg_level(self, pp: PointPlan, ch2_range: int) -> float | None:
        v = self._bg.get((pp.n, ch2_range))
        if v is None:
            return None
        return abs(tone(v[: pp.record], pp.f_actual, pp.sample_rate, pp.window))

    def _point(self, pp: PointPlan, plans: list[PointPlan]) -> tuple[Measurement, int]:
        self._background(pp, plans)
        self._set_rate(pp)
        self._gen(True, pp)
        ms = self._ref_check(pp, self._measure(pp, self._point_settle()))
        retries = 0
        if not ms.ch1_dead and self._recentre_if_needed(ms):
            ms = self._ref_check(pp, self._measure(pp, self._point_settle()))
            retries += 1
        if ms.unsettled:
            ms = self._measure(pp, 2 * self.settle_periods)
            retries += 1
        return ms, retries + int(ms.retaken)

    def _result(self, pp: PointPlan, ms: Measurement, retries: int) -> PointResult:
        s = self.s
        h = ms.h * self.k_splice
        flags = []
        if ms.clipped1:
            flags.append("ch1_clipped")
        if ms.clipped2:
            flags.append("clipped")
        if ms.flat2:
            flags.append("ch2_overload")
        if not ms.codes1 >= CH1_LOW_CODES:
            flags.append("ch1_low")
        if ms.codes2 < CH2_SMALL_CODES:
            flags.append("ch2_small")
        if ms.snr2_db < LOW_SNR_DB:
            flags.append("low_snr")
        if ms.leak_db > LEAK_LIMIT_DB or ms.ch1_dead:
            flags.append("incoherent")
        if ms.unsettled:
            flags.append("unsettled")
        if pp.fir_expected and s.info.caps & P.Caps.FIR and not ms.fir:
            flags.append("fir_off")
        bg = self._bg_level(pp, self.range[1])
        if bg is not None and abs(ms.x2) < bg * 10 ** (NEAR_FLOOR_DB / 20):
            flags.append("near_floor")
        cfg1, cfg2 = s.get_channel(0), s.get_channel(1)
        return PointResult(pp, h, h, ms.x1, ms.x2 * self.k_splice, self.range[0], self.range[1],
                           P.RANGE_VOLTS_PER_DIV[self.range[0]] * cfg1.probe_factor,
                           P.RANGE_VOLTS_PER_DIV[self.range[1]] * cfg2.probe_factor, ms.snr2_db, ms.leak_db,
                           ms.codes1, ms.codes2, ms.filter_effective, ms.fir, flags, bg, self.k_splice, retries)

    def _auto_range(self, pp: PointPlan, ms: Measurement, res: PointResult,
                    prev: tuple[PointPlan, PointResult] | None) -> Measurement | None:
        """CH2 ranging (design 3.5). A clip steps CH2 up one range (repeated while it still clips and the budget
        lasts), splices on the previous point measured on the new range, and returns the repeated measurement of
        this point (the clipped one is discarded). A small CH2 (the predicted peak on the next more sensitive range
        under 60 % of full scale) steps down after this point; the point is measured on both ranges and the ratio
        scales every later point onto the sweep's first range ('spliced')."""
        if self.ch2_mode != "auto" or ms.ch1_dead:
            return None  # no ranging on a record without a stimulus (flat twice: stuck readout or no CH1)
        r = self.range[1]
        if ms.clipped2 or ms.flat2:
            new = None
            while (new is None or new.clipped2 or new.flat2) and self.range[1] > 0 and \
                    self.switches < self.max_switches:
                r = self.range[1]
                if (new or ms).flat2:
                    self._bad_down.add(r)  # a saturated range is not tried again on the way down
                self._switch(r - 1, self._v_dc2 if (new or ms).flat2 else (new or ms).mean2)
                if prev is not None and self.splice_on:
                    ppp, pres = prev
                    self._set_rate(ppp)
                    self._gen(True, ppp)
                    again = self._measure(ppp, self._point_settle())
                    if not (again.clipped1 or again.clipped2) and _usable(again.h) and _usable(pres.h_raw):
                        self._record_splice(ppp, r, r - 1, pres.h_raw / again.h)
                        if "spliced" not in pres.flags:
                            pres.flags.append("spliced")
                    self._set_rate(pp)
                    self._gen(True, pp)
                new = self._measure(pp, self._point_settle())
            return new
        if r >= 5 or self.switches >= self.max_switches or r + 1 in self._bad_down:
            return None
        ratio = P.RANGE_VOLTS_PER_DIV[r] / P.RANGE_VOLTS_PER_DIV[r + 1]
        if ms.ac2 * ratio > DOWNRANGE_FRACTION * 128:
            return None
        self._switch(r + 1, ms.mean2)
        again = self._measure(pp, self._point_settle())
        if again.clipped2 or self._check(ms, again).flat2:
            self._switch(r, ms.mean2)  # the prediction failed: back, and do not try that range again
            self._bad_down.add(r + 1)
            return None
        if self.splice_on and _usable(again.h) and _usable(res.h_raw):
            self._record_splice(pp, r, r + 1, res.h_raw / again.h)
            res.flags.append("spliced")
        return None

    def _record_splice(self, pp: PointPlan, r_old: int, r_new: int, k: complex) -> None:
        self.k_splice = complex(k)
        self.splices.append({"f_hz": pp.f_actual, "from_range": r_old, "to_range": r_new,
                             "factor_db": round(db(abs(k)), 4), "factor_deg": round(math.degrees(np.angle(k)), 3)})

    def _switch(self, r: int, v_dc: float) -> None:
        self._set_range(1, r, v_dc)
        self.switches += 1

    # --- the sweep ------------------------------------------------------------------------------------------

    def run(self, plans: list[PointPlan], thru: ThruCal | None = None, role: str = "measure") -> SweepResult:
        if not plans:
            raise ValueError("empty frequency plan")
        t0 = time.monotonic()
        started = _dt.datetime.now().isoformat(timespec="seconds")
        res = SweepResult([], self.settle_periods, self.ch2_mode, thru.name if thru else None, role, started,
                          estimate_s=estimate_duration(plans, self.settle_periods, self.background,
                                                       self.ch2_mode == "auto"))
        saved = self._save()
        self._user_ch2_range = saved.channels[1].range if saved.channels[1].range < 6 else 5
        from .transport import DeviceError

        try:
            self._prepare(plans)
            self._prescan(plans[0])
            start_ranges = tuple(self.range)
            prev: tuple[PointPlan, PointResult] | None = None
            for i, pp in enumerate(plans):
                try:
                    ms, retries = self._point(pp, plans)
                    pr = self._result(pp, ms, retries)
                    res.points.append(pr)
                    again = self._auto_range(pp, ms, pr, prev)
                    if again is not None:  # the point was clipped and repeated on a less sensitive range
                        res.points.pop()
                        pr = self._result(pp, again, retries + 1)
                        res.points.append(pr)
                except DeviceError as e:
                    if e.status == P.Status.E_BUSY:
                        res.stopped_early = (f"the scope answered E_BUSY twice at {pp.f_actual:.6g} Hz "
                                             f"({P.busy_hint(e.detail, e.text)}); {len(res.points)} of {len(plans)} "
                                             "points measured")
                        break
                    raise
                prev = (pp, pr)
                if self.progress is not None:
                    self.progress(i + 1, len(plans), f"{pp.f_actual:.6g} Hz {pr.mag_db:.2f} dB")
            res.meta["start_ranges"] = {"CH1": start_ranges[0], "CH2": start_ranges[1]}
        finally:
            errs = self._restore(saved)
            if errs:
                res.caveats.append("could not restore every setting: " + "; ".join(errs))
        if thru is not None:
            for p in res.points:
                thru.apply(p)
        if self._flat_twice:
            fl = ", ".join(f"{f:.6g}" for f in self._flat_twice[:6]) + (" ..." if len(self._flat_twice) > 6 else "")
            self.caveats.append(f"CH1's record was flat or had no tone even after a retake at {len(self._flat_twice)} "
                                f"frequency(ies) ({fl} Hz): a stuck or saturated ADC record or no stimulus on CH1; "
                                "those points are flagged ch1_low + incoherent and have no H")
        res.meta["flat_retakes"] = self.flat_retakes
        res.duration_s = time.monotonic() - t0
        res.range_switches = self.switches
        res.splices = self.splices
        res.caveats = self.caveats + res.caveats
        res.meta.update({"plan": plan_summary(plans), "background": self.background,
                         "probes": [saved.channels[0].probe_factor, saved.channels[1].probe_factor]})
        return res


# ---------------------------------------------------------------------------------------------------------
# The on-device sweep (protocol 1.20, BODE_CTRL / READ_BODE; firmware/AGENTS.md section 42)


def device_settle_code(settle: str | float | int | None) -> int:
    """BODE_CTRL byte 2 for a settle preset (the device knows only the four presets)."""
    periods = parse_settle(settle)
    for i, name in enumerate(P.BODE_SETTLES):
        if SETTLE_PRESETS[name] == periods:
            return i
    raise ValueError(f"settle {settle!r}: the device sweep knows only the presets "
                     f"{', '.join(f'{n} ({SETTLE_PRESETS[n]})' for n in P.BODE_SETTLES)}")


def device_ch2_code(ch2_range: str | float | int) -> int:
    """BODE_CTRL byte 3: 'auto' 0, 'fixed' 1 (hold: CH2's range when the sweep starts; the screen says Constant, the
    V/div of the CH2 menu, fw 0.2.94)."""
    v = str(ch2_range).strip().lower()
    if v not in P.BODE_CH2_MODES:
        raise ValueError(f"ch2_range {ch2_range!r}: the device sweep takes 'auto' or 'fixed' (set CH2's V/div first "
                         "for a fixed range)")
    return P.BODE_CH2_MODES.index(v)


DEVICE_CAVEATS = (
    "device sweep: the scope measured and ranged by itself (bode_dsp.c, integer DFT on the same plan); the table has "
    "no SNR, leakage or code levels (the flags carry them), and its noise estimate (period differences) reads a "
    "slow transient as low_snr where the host says unsettled",)


def point_from_row(row: P.BodeRow, pp: PointPlan, probes: tuple[float, float] = (1.0, 1.0)) -> PointResult:
    """A READ_BODE row as the host tool's PointResult (f from P through the plan; H, V1, V2 from centi-units)."""
    mag, ph = row.mag_cdb / 100.0, row.phase_cdeg / 100.0
    h = 10 ** (mag / 20) * complex(math.cos(math.radians(ph)), math.sin(math.radians(ph)))
    x1 = 10 ** (row.v1_cdbv / 2000.0) * math.sqrt(2) + 0j
    x2 = 10 ** (row.v2_cdbv / 2000.0) * math.sqrt(2) * complex(math.cos(math.radians(ph)), math.sin(math.radians(ph)))
    flags = row.flag_names
    fir = not pp.legacy and pp.fir_expected and "fir_off" not in flags
    filt = "legacy" if pp.legacy else ("cic3+fir" if fir else "cic3")
    r1, r2 = min(row.ch1_range, 5), min(row.ch2_range, 5)
    return PointResult(pp, h, h, x1, x2, row.ch1_range, row.ch2_range, P.RANGE_VOLTS_PER_DIV[r1] * probes[0],
                       P.RANGE_VOLTS_PER_DIV[r2] * probes[1], math.nan, math.nan, math.nan, math.nan, filt, fir,
                       flags, None, 1 + 0j, 1 if row.retried else 0)


def result_from_device(hdr: P.BodeHeader, rows: list[P.BodeRow], *, probes: tuple[float, float] = (1.0, 1.0),
                       estimate_s: float = 0.0, started: str = "") -> SweepResult:
    """The device's table as a SweepResult (the same structure as a host sweep, so the two compare point by point).
    The plan comes from the header's start / stop / points (the device's planner is the host's ``plan_sweep``); a row
    whose P is not in it gets its own ``plan_point``."""
    plans = plan_sweep(hdr.start_chz / 100.0, hdr.stop_chz / 100.0, max(1, hdr.points)) if hdr.start_chz else []
    by_p = {pp.period: pp for pp in plans}
    pts = []
    for r in rows:
        if not r.measured or not r.period:
            continue
        pp = by_p.get(r.period) or plan_point(GEN_CLOCK_HZ / r.period)
        pts.append(point_from_row(r, pp, probes))
    role = "thru" if hdr.view & P.BodeHeader.V_THRU_SWEEP else "measure"
    res = SweepResult(pts, float(SETTLE_PRESETS[P.BODE_SETTLES[hdr.settle]]) if hdr.settle < 4 else math.nan,
                      P.BODE_CH2_MODES[hdr.ch2_mode] if hdr.ch2_mode < 2 else str(hdr.ch2_mode),
                      "device" if hdr.thru_applied and role == "measure" else None, role, started,
                      duration_s=hdr.elapsed_ms / 1000.0, estimate_s=estimate_s, range_switches=hdr.switches)
    if hdr.state != 2:
        res.stopped_early = (f"the device sweep ended {hdr.state_name}"
                             f"{': ' + hdr.reason_text if hdr.reason_text else ''}; {len(pts)} of {hdr.planned} points "
                             "measured")
    res.caveats = list(DEVICE_CAVEATS)
    for bit, ch in ((P.BodeHeader.V_CH1_OFFSET_LIMIT, 1), (P.BodeHeader.V_CH2_OFFSET_LIMIT, 2)):
        if hdr.view & bit:
            res.caveats.append(f"CH{ch}: offset range exhausted (the trace could not be centred within the offset "
                               "PWM's 0..2000): the DC level is beyond this range's window; see the ch2_overload / "
                               "clipped flags")
    if hdr.reason == 8:
        res.caveats.append("ADC readout fault: on FPGA 0x153A+ (fw 0.2.83) the capture self-check flagged a core "
                           "during the sweep, on older bitstreams the pre-scan clipped at 5 V/div with a small "
                           "fundamental; the scope stops the sweep rather than produce a flagged table. Check "
                           "scope_health (capture stages, flagged cores), run the calibration wizard (it trains the "
                           "capture stages) and check the FPGA bitstream before trusting any capture")
    if hdr.reason == 3:
        res.caveats.append("CH2's front end is saturated on the held range (a flat record: a DC level against the "
                           "probe reference beyond this range's input window). Remedy: wiring A (DUT ground on the "
                           "probe ground clips), ch2_range='auto', or a less sensitive CH2 range")
    if res.thru_cal == "device":
        res.caveats.append(f"the scope's own thru calibration was applied (state {hdr.thru_state}): H is divided by "
                           "the thru's H at the same (P, N) on the scope")
    res.meta = {"where": "device", "sweep_id": hdr.sweep_id, "state": hdr.state_name, "reason": hdr.reason_text,
                "thru": hdr.thru_state, "thru_applied": hdr.thru_applied, "view_shown": bool(hdr.view & 1),
                "plan": plan_summary(plans) if plans else {}, "probes": list(probes)}
    return res


def device_sweep(scope: Any, start: float, stop: float, points: int, *, settle: str | float = "normal",
                 ch2_range: str = "auto", background: bool = True, thru: str | ThruCal | None = None,
                 close_view: bool = False, poll_s: float = 0.2, timeout_s: float | None = None,
                 sleep: Callable[[float], None] = time.sleep,
                 progress: Callable[[int, int, str], None] | None = None) -> SweepResult:
    """Run one sweep on the scope (BODE_CTRL action 1 with the settings, or action 5 for ``thru='measure'``), poll
    READ_BODE until the state leaves 'running', read the rows and return them as a SweepResult. ``thru``: None (the
    scope's thru is switched off if it was applied, so H is raw), 'device' (apply the scope's stored thru), 'measure'
    (this sweep measures it: both probe tips on the DUT input) or a host ThruCal (applied here to the raw rows).
    The Bode view stays open unless ``close_view`` (closing gives the scope's settings back)."""
    from .transport import DeviceError

    s = scope
    caps = s.info.caps
    if s.info.proto_minor < 20:
        raise BodeError("the device sweep needs firmware 0.2.66 or later (protocol 1.20)")
    if not caps & P.Caps.DECIM or not caps & P.Caps.GENERATOR:
        raise BodeError("the device sweep needs the decimation filter and the generator (FPGA 0x1534+)")
    settle_code, ch2_code = device_settle_code(settle), device_ch2_code(ch2_range)
    lo, hi = sorted((float(start), float(stop)))
    if not (MIN_FREQ_HZ <= lo and hi <= MAX_FREQ_HZ) or not 1 <= points <= 200:
        raise ValueError(f"device sweep: {lo:g} .. {hi:g} Hz, {points} points (1 Hz .. 10 MHz, 1..200 points)")
    probes = (s.get_channel(0).probe_factor, s.get_channel(1).probe_factor)
    notes: list[str] = []

    def ctrl(req: P.BodeCtrlReq | int) -> P.BodeCtrlResp:
        try:
            return s.bode_ctrl(req)
        except DeviceError as e:
            if e.status == P.Status.E_BUSY:
                raise BodeError(f"the scope answered E_BUSY: {P.busy_hint(e.detail, e.text)}") from e
            if e.status == P.Status.E_STATE and e.detail == 1:
                raise BodeError("the Bode view is not available: a picture or waveform viewer is open on the scope") \
                    from e
            if e.status == P.Status.E_STATE and e.detail == 2:
                raise BodeError("a sweep is already running on the scope (started at the scope?): wait for it or "
                                "stop it there") from e
            if e.status == P.Status.E_STATE and e.detail == 4:
                raise BodeError("no thru calibration on the scope: run thru_cal='measure' first (both probe tips "
                                "on the DUT input)") from e
            raise

    st = ctrl(P.BODE_STATUS).hdr
    if st.running:
        raise BodeError("a sweep is already running on the scope (started at the scope?): wait for it or stop it there")
    if thru == "device":
        ctrl(P.BODE_THRU_ON)
    elif thru != "measure" and st.thru_applied:
        ctrl(P.BODE_THRU_OFF)
        notes.append("the scope's own thru calibration was applied: switched off for this sweep (raw H); it stays "
                     "stored on the scope (thru_cal='device' applies it)")
    action = P.BODE_THRU_MEASURE if thru == "measure" else P.BODE_RUN
    t0 = time.monotonic()
    started = _dt.datetime.now().isoformat(timespec="seconds")
    resp = ctrl(P.BodeCtrlReq.make(action, lo, hi, points, settle_code, ch2_code, background))
    est = resp.estimate_ms / 1000.0
    sid = resp.hdr.sweep_id
    limit = timeout_s if timeout_s is not None else 3 * est + 30.0
    seen = -1
    while True:
        hdr, rows = s.read_bode()
        if hdr.sweep_id != sid:
            raise BodeError(f"the scope started another sweep (id {hdr.sweep_id}, ours {sid}) while this one ran")
        if not hdr.running:
            break
        if progress is not None and hdr.measured != seen:
            seen = hdr.measured
            done = [r for r in rows if r.measured]
            last = f"{done[-1].f_hz:.6g} Hz {done[-1].mag_cdb / 100:.2f} dB" if done else "starting"
            progress(hdr.measured, hdr.planned, last)
        if time.monotonic() - t0 > limit:
            ctrl(P.BODE_ABORT)
            raise BodeError(f"the device sweep did not end within {limit:.0f} s (estimate {est:.0f} s): aborted")
        sleep(poll_s)
    res = result_from_device(hdr, rows, probes=probes, estimate_s=est, started=started)
    res.caveats = notes + res.caveats
    if isinstance(thru, ThruCal):
        res.thru_cal = thru.name
        for p in res.points:
            thru.apply(p)
    if close_view:
        res.meta["view_shown"] = bool(ctrl(P.BODE_CLOSE).hdr.view & P.BodeHeader.V_SHOWN)
    else:
        res.caveats.append("the scope still shows its Bode plot (its own acquisition is off until the person leaves "
                           "it: Menu > Scope, close_view=true, or, fw 0.2.105+, the next host setting or capture, "
                           "which closes it and gives the scope settings back)")
    return res


# ---------------------------------------------------------------------------------------------------------
# Caveats an engineer needs with the numbers


def fmt_f3(hz: float) -> str:
    """A frequency with three significant digits, a space and the unit, as the scope's note writes it (bode_ui.c
    bu_fmt_f3, the same integer rounding on mHz): 541 kHz, 54.1 kHz, 1.23 MHz, 980 Hz."""
    f = int(round(hz * 1000))
    sc, unit = (10**9, "MHz") if f >= 999_500_000 else (10**6, "kHz") if f >= 999_500 else (10**3, "Hz")
    if f * 20 >= 1999 * sc:
        return f"{(f + sc // 2) // sc} {unit}"
    if f * 200 >= 1999 * sc:
        v = (f * 10 + sc // 2) // sc
        return f"{v // 10}.{v % 10} {unit}"
    v = (f * 100 + sc // 2) // sc
    return f"{v // 100}.{v % 100:02d} {unit}"


def ch2_floor_hz(points: list[PointResult]) -> float | None:
    """The frequency above which CH2 is too small to measure (the scope's bu_ch2_floor_f): of the ch2_small points on
    CH2's finest range (100 mV/div), the lowest of those above every other point (the run that ends the span); when
    the highest point is not one of them, the lowest of them; None when there is none."""
    def floor_pt(p: PointResult) -> bool:
        return "ch2_small" in p.flags and p.ch2_range >= CH2_FINE_RANGE

    small = [p.plan.f_actual for p in points if floor_pt(p)]
    if not small:
        return None
    above = max((p.plan.f_actual for p in points if not floor_pt(p)), default=0.0)
    tail = [f for f in small if f > above]
    return min(tail) if tail else min(small)


def ch2_floor_text(hz: float) -> str:
    """The scope's two-line note as one sentence (fw 0.2.99, owner 2026-10-20 16:22)."""
    return (f"CH2 too small above {fmt_f3(hz)} to measure: set Stop below it or press AUTO SET. "
            "Less pickup: short ground lead on CH2.")


def result_caveats(res: SweepResult) -> list[str]:
    out = []
    # fw 0.2.99: CH2 under the floor on its finest range: what it means and what helps, first
    fl_hz = ch2_floor_hz(res.points)
    if fl_hz is not None:
        out.append(ch2_floor_text(fl_hz))
    flagged: dict[str, int] = {}
    for p in res.points:
        for f in p.flags:
            flagged[f] = flagged.get(f, 0) + 1
    text = {
        "clipped": "CH2 clipped (set ch2_range='auto' or a larger V/div)",
        "ch1_clipped": "CH1 clipped",
        "ch2_overload": "CH2 saturated by the DUT's DC level (a flat record): wiring A (DUT ground on the probe clips), "
                        "ch2_coupling='ac' or a less sensitive CH2 range",
        "ch1_low": f"CH1 fundamental below {CH1_LOW_CODES:g} codes: the DUT input loads the 1 kOhm source",
        "ch2_small": f"CH2 below {CH2_SMALL_CODES:g} codes peak: under what the ADC measures (its linearity alone "
                     "gives about +-1 dB / +-6 deg, and pickup can dominate); left out of the markers and drawn grey",
        "near_floor": f"CH2 less than {NEAR_FLOOR_DB:g} dB above the generator-off background in its bin (crosstalk, "
                      "offset-PWM ripple, pickup): treat as an upper bound",
        "low_snr": f"CH2 less than {LOW_SNR_DB:g} dB above the record's noise",
        "unsettled": "the two record halves disagreed after a retry with twice the settle time: use "
                     "settle='resonant' (or 'high_q')",
        "incoherent": "energy beside the fundamental on CH1: the stimulus is not locked to the sample clock or the "
                      "input is not the generator",
        "fir_off": "the compensation FIR did not engage where the plan expected it",
        "no_cal": "the thru calibration has no point with this frequency plan (rerun it with the same start, stop "
                  "and points)",
        "cal_range": "taken on other ranges than the thru calibration (the per-range gain error, ~1-2 %, is not "
                     "removed)",
        "cal_flagged": "the thru calibration's point was itself flagged",
    }
    for f, n in sorted(flagged.items()):
        if f in text:
            out.append(f"{n} point(s) {f}: {text[f]}")
    if res.splices:
        out.append(f"CH2 changed range {len(res.splices)} time(s); each later point is scaled onto the first range "
                   "by the ratio measured at the switch point (flag 'spliced')")
    if res.thru_cal is None and res.role == "measure":
        out.append("no thru calibration applied: CH1 / CH2 gain (1-2 %), skew and probe mismatch are in the result "
                   "(run thru_cal='measure' with both probes on the DUT input once)")
    out.append("H = CH2 / CH1 at the DUT input node, independent of the 1 kOhm source; a filter specified for a "
               "50 / 600 Ohm source shows its V2 / V1, not the insertion response")
    return out


# ---------------------------------------------------------------------------------------------------------
# CLI


def _fmt_row(r: dict[str, Any]) -> str:
    fl = ",".join(r["flags"])
    mag = math.nan if r["mag_db"] is None else r["mag_db"]
    ph = math.nan if r["phase_deg"] is None else r["phase_deg"]
    return (f"{r['f_actual']:>12.4f} {mag:>9.3f} {ph:>9.2f} {r['ch2_volts_per_div']:>7g} "
            f"{r['sample_rate']:>11.6g} {r['filter']:>9} {fl}")


def main(argv: Iterable[str] | None = None) -> int:
    from .units import parse_quantity

    ap = argparse.ArgumentParser(prog="fnirsi1013d-bode", description=(
        "Host-driven Bode sweep on the FNIRSI 1013D (generator on the calibration post, CH1 = DUT input, CH2 = DUT "
        "output). Drives the calibration pin: needs --yes. " + CONNECTION_TEXT))
    ap.add_argument("--port", default="auto", help="serial port, tcp://host:port (simulator) or auto")
    ap.add_argument("--start", default="100Hz")
    ap.add_argument("--stop", default="1MHz")
    ap.add_argument("--points", type=int, default=50)
    ap.add_argument("--settle", default="normal", help=f"{', '.join(SETTLE_PRESETS)} or periods")
    ap.add_argument("--ch2-range", default="auto", help="auto, fixed (the current CH2 range) or a V/div")
    ap.add_argument("--ch2-coupling", choices=("dc", "ac"), default="dc",
                    help="ac removes the DUT's DC from CH2 (wiring B: the DUT sits 1.24 V below the probe reference)")
    ap.add_argument("--no-background", action="store_true", help="skip the generator-off floor captures")
    ap.add_argument("--thru", default=None, help="'measure' to record a thru calibration, or a calibration file")
    ap.add_argument("--thru-out", default=None, help="where --thru measure saves the calibration (JSON)")
    ap.add_argument("--csv", default=None)
    ap.add_argument("--json", default=None)
    ap.add_argument("--plan-only", action="store_true", help="print the frequency plan and the estimate, no scope")
    ap.add_argument("--max-duration", type=float, default=120.0, help="refuse sweeps estimated longer (s)")
    ap.add_argument("--yes", action="store_true", help="confirm: the calibration pin outputs a 3.3 V square wave")
    ap.add_argument("--write-vectors", metavar="PATH", help="write the planner vectors (JSON) and exit")
    a = ap.parse_args(list(argv) if argv is not None else None)
    if a.write_vectors:
        Path(a.write_vectors).write_bytes((json.dumps(plan_vectors(), indent=1) + "\n").encode())
        print("wrote", a.write_vectors)
        return 0
    plans = plan_sweep(parse_quantity(a.start, "Hz"), parse_quantity(a.stop, "Hz"), a.points)
    settle = parse_settle(a.settle)
    est = estimate_duration(plans, settle, not a.no_background, a.ch2_range == "auto")
    if a.plan_only:
        print(f"{'f_req':>12} {'f_actual':>12} {'P':>9} {'N':>6} {'p/q':>9} {'L':>5} {'settle_s':>9}")
        for pp in plans:
            print(f"{pp.f_requested:>12.4f} {pp.f_actual:>12.4f} {pp.period:>9} {pp.n:>6} {f'{pp.p}/{pp.q}':>9} "
                  f"{pp.record:>5} {pp.settle_s(settle):>9.4f}")
        print(f"{len(plans)} points, estimated {est:.1f} s")
        return 0
    if est > a.max_duration:
        print(f"estimated {est:.0f} s > --max-duration {a.max_duration:g} s; raise it or narrow the sweep",
              file=sys.stderr)
        return 2
    if not a.yes:
        print("this drives the calibration pin (3.3 V square wave into the DUT); rerun with --yes", file=sys.stderr)
        return 2
    from .scope import Scope

    thru = None
    if a.thru and a.thru != "measure":
        thru = ThruCal.load(a.thru)
    with Scope.connect(a.port) as s:
        s.ensure_control()
        try:
            r = BodeRunner(s, settle=settle, ch2_range=a.ch2_range, background=not a.no_background,
                           progress=lambda i, n, t: print(f"  {i}/{n} {t}", file=sys.stderr),
                           ch2_coupling=a.ch2_coupling)
            res = r.run(plans, thru, role="thru" if a.thru == "measure" else "measure")
        finally:
            s.end_control(keep_settings=True)
    print(f"{'f_hz':>12} {'mag_db':>9} {'phase':>9} {'ch2V/d':>7} {'rate':>11} {'filter':>9} flags")
    for row in res.table():
        print(_fmt_row(row))
    print(json.dumps(res.summary(), indent=1, default=str))
    for c in res.caveats + result_caveats(res):
        print("caveat:", c)
    if a.thru == "measure":
        out = a.thru_out or f"thru_{_dt.datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
        print("thru calibration saved:", ThruCal.from_sweep(res, Path(out).stem, tuple(res.meta["probes"])).save(out))
    if a.csv:
        print("csv:", res.write_csv(a.csv))
    if a.json:
        print("json:", res.write_json(a.json))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())


__all__ = ["BodeError", "BodeRunner", "Measurement", "PointPlan", "PointResult", "SweepResult", "ThruCal",
           "CH2_FINE_RANGE", "CONNECTION_TEXT", "SETTLE_PRESETS", "asdict", "ch2_floor_hz", "ch2_floor_text",
           "estimate_duration", "fmt_f3", "markers", "measure_pair",
           "parse_settle", "plan_point", "plan_sweep", "plan_vectors", "residual_noise", "result_caveats", "tone"]
