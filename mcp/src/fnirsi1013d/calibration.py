"""Per-unit, per-core voltage calibration (bench test T8).

The firmware's scaling (protocol 8.5) puts 0 V at ``zero_code = 128 - dcoffset x 100 / dc_shift_value`` on the
mean of the two compensated ADC cores. On the owner's unit (T8, fw 0.1.7, 2026-10-18) the gains match within ~1 %
but the zero does not: the two cores sit 5 codes apart, while the stored compensation corrects only 4. The zero
also follows the offset PWM with a different slope than the formula assumes. This module keeps a measured model
per channel, range and core:

    raw_code = zero + slope x (offset_pwm - pwm_ref) + volts_at_bnc / gain

``gain`` is volts per code at the BNC with a 1x probe. When it is missing, the firmware's uv_per_lsb is used.
``slope`` is ADC codes per offset-PWM count; when it is missing, the firmware's -100 / dc_shift_value is used.
Fits use every stored point: zero always, 1/gain when the points span two voltages, slope when they span two
offsets. Saturated points (mean below 3 or above 252) are ignored.

The file is JSON (``points`` are the raw measurements, ``fit`` is derived from them):

    {"unit": "1013D-0001", "points": [{"channel": 0, "range": 3, "offset_pwm": 903, "volts": 1.0,
      "meanA": 178.42, "meanB": 173.09, "fw_volts_per_lsb": 0.01744, "dc_shift_value": 385, "source": "..."}],
     "fit": {"0": {"3": {"A": {"zero": .., "pwm_ref": .., "slope": .., "gain": .., "n": .., "rms": ..}, "B": ..}}}}

T8 on the owner's unit (fw 0.1.7, CH1, 45 points): slope -0.259..-0.265 codes per count on every range (firmware
-100/385 = -0.260), gains within 1.5 % of uv_per_lsb, 0 V at dcoffset 0 at A 132.5 / B 127.0 on every range: the
offset is injected after the attenuator. So besides the per-range fit there is a channel model per core,
``zero = Z - slope x dcoffset`` with ``dcoffset = offset_pwm - dc_calibration_offset[range]``: it fills in the
slope of ranges measured at one offset only, and converts ranges with no points at all (gain from the firmware).

CLI: ``python -m fnirsi1013d.calibration --import backups/t8_CH1_*.json --out <file>`` builds a file from the
lead's T8 dataset. ``--show <file>`` prints the fit.
"""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable

import numpy as np

CORES = ("A", "B")
#: means outside this window are treated as clipped (an ADC core saturates at 0 / 255)
VALID_LO, VALID_HI = 3.0, 252.0


@dataclass
class CoreFit:
    zero: float
    pwm_ref: float
    #: codes per offset-PWM count; None: the firmware's -100 / dc_shift_value
    slope: float | None = None
    #: volts per code at the BNC (1x); None: the firmware's uv_per_lsb
    gain: float | None = None
    n: int = 0
    rms: float = 0.0
    slope_measured: bool = False
    gain_measured: bool = False

    def zero_at(self, offset_pwm: float, fw_slope: float) -> float:
        s = self.slope if self.slope is not None else fw_slope
        return self.zero + s * (offset_pwm - self.pwm_ref)

    def to_dict(self) -> dict[str, Any]:
        return {k: (round(v, 6) if isinstance(v, float) else v) for k, v in vars(self).items()}

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> "CoreFit":
        return cls(**{k: d[k] for k in ("zero", "pwm_ref", "slope", "gain", "n", "rms", "slope_measured",
                                        "gain_measured") if k in d})


def fw_slope(dc_shift_value: int | float | None) -> float:
    """Codes per PWM count implied by the firmware formula (zero_code = 128 - dcoffset x 100 / dc_shift_value)."""
    return -100.0 / dc_shift_value if dc_shift_value else 0.0


@dataclass
class UnitCalibration:
    unit: str = ""
    points: list[dict[str, Any]] = field(default_factory=list)
    fit: dict[int, dict[int, dict[str, CoreFit]]] = field(default_factory=dict)
    #: per channel and core: {"Z": raw code of 0 V at dcoffset 0, "slope": codes per count, "n_ranges": ...}
    channel_fit: dict[int, dict[str, dict[str, float]]] = field(default_factory=dict)
    path: Path | None = None

    # --- persistence ---------------------------------------------------------------------------------

    @classmethod
    def load(cls, path: str | Path) -> "UnitCalibration":
        p = Path(path)
        d = json.loads(p.read_text(encoding="utf-8"))
        cal = cls(unit=d.get("unit", ""), points=list(d.get("points", [])), path=p)
        cal.refit()
        return cal

    @classmethod
    def load_or_new(cls, path: str | Path, unit: str = "") -> "UnitCalibration":
        p = Path(path)
        if p.is_file():
            return cls.load(p)
        return cls(unit=unit, path=p)

    def to_json(self) -> dict[str, Any]:
        return {"unit": self.unit, "model": "raw = zero + slope*(offset_pwm - pwm_ref) + volts/gain",
                "points": self.points,
                "fit": {str(ch): {str(r): {c: f.to_dict() for c, f in cores.items()} for r, cores in rr.items()}
                        for ch, rr in self.fit.items()},
                "channel_fit": {str(ch): cores for ch, cores in self.channel_fit.items()}}

    def save(self, path: str | Path | None = None) -> Path:
        p = Path(path or self.path or "calibration.json")
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes((json.dumps(self.to_json(), indent=1) + "\n").encode())
        self.path = p
        return p

    # --- fitting -------------------------------------------------------------------------------------

    def add_point(self, *, channel: int, range_index: int, offset_pwm: int, volts: float, meanA: float,
                  meanB: float, fw_volts_per_lsb: float, dc_shift_value: int, source: str = "",
                  center_cal: int | None = None) -> None:
        """``center_cal`` = dc_calibration_offset[range] (offset_pwm at dcoffset 0), for the channel model."""
        self.points.append({"channel": int(channel), "range": int(range_index), "offset_pwm": int(offset_pwm),
                            "volts": float(volts), "meanA": float(meanA), "meanB": float(meanB),
                            "fw_volts_per_lsb": float(fw_volts_per_lsb), "dc_shift_value": int(dc_shift_value),
                            "center_cal": None if center_cal is None else int(center_cal), "source": source})

    def refit(self) -> None:
        self.fit = {}
        self.channel_fit = {}
        groups: dict[tuple[int, int], list[dict[str, Any]]] = {}
        for p in self.points:
            groups.setdefault((p["channel"], p["range"]), []).append(p)
        for (ch, rng), pts in groups.items():
            for core in CORES:
                f = _fit_core(pts, core)
                if f is not None:
                    self.fit.setdefault(ch, {}).setdefault(rng, {})[core] = f
        # channel model: the slope is range-independent; ranges fitted at one offset borrow the measured one
        for ch, rr in self.fit.items():
            for core in CORES:
                fits = [(rng, cores[core]) for rng, cores in rr.items() if core in cores]
                slopes = [f.slope for _, f in fits if f.slope_measured and f.slope is not None]
                slope = float(np.median(slopes)) if slopes else None
                if slope is not None:
                    for _, f in fits:
                        if not f.slope_measured:
                            f.slope = slope
                zs = []
                for rng, f in fits:
                    centers = [p["center_cal"] for p in groups[(ch, rng)] if p.get("center_cal") is not None]
                    if centers:
                        s = f.slope if f.slope is not None else fw_slope(np.median(
                            [p["dc_shift_value"] for p in groups[(ch, rng)]]))
                        zs.append(f.zero + s * (float(np.median(centers)) - f.pwm_ref))
                if zs:
                    self.channel_fit.setdefault(ch, {})[core] = {
                        "Z": round(float(np.median(zs)), 4), "slope": None if slope is None else round(slope, 5),
                        "n_ranges": len(zs), "spread": round(float(np.ptp(zs)), 4)}

    def core_fit(self, channel: int, range_index: int, core: str) -> CoreFit | None:
        return self.fit.get(channel, {}).get(range_index, {}).get(core)

    def has(self, channel: int, range_index: int) -> bool:
        r = self.fit.get(channel, {}).get(range_index, {})
        return all(c in r for c in CORES)

    def has_channel(self, channel: int) -> bool:
        return all(c in self.channel_fit.get(channel, {}) for c in CORES)


def _fit_core(pts: list[dict[str, Any]], core: str) -> CoreFit | None:
    key = "mean" + core
    use = [p for p in pts if VALID_LO <= p[key] <= VALID_HI]
    if not use:
        return None
    pwm = np.array([p["offset_pwm"] for p in use], float)
    v = np.array([p["volts"] for p in use], float)
    y = np.array([p[key] for p in use], float)
    fw_gain = float(np.median([p["fw_volts_per_lsb"] for p in use]))
    fws = fw_slope(float(np.median([p["dc_shift_value"] for p in use])))
    pwm_ref = float(np.median(pwm))
    fit_gain = np.ptp(v) > 1e-6 and len(set(v.round(6))) >= 2
    fit_slope = np.ptp(pwm) > 0.5
    # unknowns: zero, [1/gain], [slope]; the fixed parts are moved to the right-hand side
    cols = [np.ones_like(y)]
    rhs = y.copy()
    if fit_gain:
        cols.append(v)
    else:
        rhs -= v / fw_gain
    if fit_slope:
        cols.append(pwm - pwm_ref)
    else:
        rhs -= fws * (pwm - pwm_ref)
    A = np.column_stack(cols)
    sol, *_ = np.linalg.lstsq(A, rhs, rcond=None)
    i = 1
    zero = float(sol[0])
    gain = None
    if fit_gain:
        inv = float(sol[i])
        i += 1
        gain = 1.0 / inv if inv > 0 else None
    slope = float(sol[i]) if fit_slope else None
    pred = A @ sol
    rms = float(math.sqrt(np.mean((rhs - pred) ** 2))) if len(y) > len(sol) else 0.0
    return CoreFit(zero=zero, pwm_ref=pwm_ref, slope=slope, gain=gain, n=len(y), rms=rms,
                   slope_measured=bool(fit_slope), gain_measured=gain is not None)


# --- conversion ---------------------------------------------------------------------------------------------


@dataclass
class ChannelScale:
    """Code <-> volt conversion for one channel in its current state, per ADC core, at the BNC (1x).
    ``zero_a``/``zero_b`` are raw (uncompensated) core codes of 0 V; ``gain_*`` volts per code."""

    zero_a: float
    zero_b: float
    gain_a: float
    gain_b: float
    adc_a_comp: int
    adc_b_comp: int
    probe: float
    source: str  # "unit-calibration" or "firmware"

    def volts_raw(self, raw: np.ndarray, core: str) -> np.ndarray:
        z, g = (self.zero_a, self.gain_a) if core == "A" else (self.zero_b, self.gain_b)
        return (np.asarray(raw, np.float64) - z) * g * self.probe

    def volts_compensated(self, codes: np.ndarray, core: str) -> np.ndarray:
        """Codes that already include the firmware's per-core compensation (READ_TRACE)."""
        comp = self.adc_a_comp if core == "A" else self.adc_b_comp
        return self.volts_raw(np.asarray(codes, np.float64) - comp, core)

    def volts_mean_compensated(self, codes: np.ndarray) -> np.ndarray:
        """Mean of the two compensated cores (ACQUIRE_AVG), converted with the cores' mean zero and gain."""
        z = ((self.zero_a + self.adc_a_comp) + (self.zero_b + self.adc_b_comp)) / 2
        g = (self.gain_a + self.gain_b) / 2
        return (np.asarray(codes, np.float64) - z) * g * self.probe

    def raw_a_for_volts(self, volts: float) -> float:
        """Raw core-A code of a voltage at the probe tip (the trigger compares core A raw)."""
        return volts / (self.gain_a * self.probe) + self.zero_a

    def volts_for_raw_a(self, code: float) -> float:
        return float((code - self.zero_a) * self.gain_a * self.probe)

    @property
    def zero_compensated(self) -> float:
        return ((self.zero_a + self.adc_a_comp) + (self.zero_b + self.adc_b_comp)) / 2

    @property
    def volts_per_lsb(self) -> float:
        return (self.gain_a + self.gain_b) / 2 * self.probe


def channel_scale(ch: int, cfg: Any, cal_resp: Any, unit: UnitCalibration | None) -> ChannelScale:
    """Scale for channel ``ch`` with ChannelConfig ``cfg`` and CalResp ``cal_resp``; uses ``unit`` when it has a
    fit for the channel's range, else the firmware formula."""
    ca, cb = int(cal_resp.adc_a_comp), int(cal_resp.adc_b_comp)
    probe = cfg.probe_factor
    fw_g = cfg.volts_per_lsb
    fs = fw_slope(getattr(cal_resp, "dc_shift_value", 0))
    if unit is not None and unit.has(ch, cfg.range):
        fa = unit.core_fit(ch, cfg.range, "A")
        fb = unit.core_fit(ch, cfg.range, "B")
        assert fa is not None and fb is not None
        return ChannelScale(fa.zero_at(cfg.offset_pwm, fs), fb.zero_at(cfg.offset_pwm, fs), fa.gain or fw_g,
                            fb.gain or fw_g, ca, cb, probe, "unit-calibration")
    centers = getattr(cal_resp, "dc_calibration_offset", ())
    if unit is not None and unit.has_channel(ch) and cfg.range < len(centers):
        dco = cfg.offset_pwm - centers[cfg.range]
        za, zb = (unit.channel_fit[ch][c]["Z"] + (unit.channel_fit[ch][c]["slope"] or fs) * dco for c in CORES)
        return ChannelScale(za, zb, fw_g, fw_g, ca, cb, probe, "unit-calibration (channel model, firmware gain)")
    z = cfg.zero_code
    return ChannelScale(z - ca, z - cb, fw_g, fw_g, ca, cb, probe, "firmware")


# --- import of the lead's T8 files --------------------------------------------------------------------------


def import_t8(files: Iterable[str | Path], cal: UnitCalibration | None = None) -> UnitCalibration:
    """Rows of backups/t8_CH<n>_<V>.json (lead's bench format) as calibration points."""
    import re

    cal = cal or UnitCalibration()
    for f in files:
        d = json.loads(Path(f).read_text(encoding="utf-8"))
        ch = int(str(d.get("channel", "CH1")).upper().replace("CH", "")) - 1
        m = re.search(r"dc_shift_value=(\d+)", str(d.get("cal", "")))
        dsv = int(m.group(1)) if m else 385
        for r in d["rows"]:
            if r.get("probe_index", 1) != 1:
                continue
            cal.add_point(channel=ch, range_index=r["range"], offset_pwm=r["offset_pwm"], volts=d["applied_volts"],
                          meanA=r["meanA"], meanB=r["meanB"], fw_volts_per_lsb=r["uv_per_lsb_q8"] / 256 * 1e-6,
                          dc_shift_value=dsv, source=f"T8 {Path(f).name} fw {d.get('fw', '?')}",
                          center_cal=r.get("center_cal"))
    cal.refit()
    return cal


def describe(cal: UnitCalibration) -> list[str]:
    out = []
    for ch, rr in sorted(cal.fit.items()):
        for rng, cores in sorted(rr.items()):
            parts = []
            for c in CORES:
                f = cores.get(c)
                if f is None:
                    parts.append(f"{c}: -")
                    continue
                g = f"{f.gain * 1e3:.3f} mV/LSB" if f.gain else "fw gain"
                s = f"{f.slope:+.3f}/count" if f.slope is not None else "fw slope"
                parts.append(f"{c}: zero {f.zero:.2f} @pwm {f.pwm_ref:.0f}, {g}, {s}, n={f.n}, rms {f.rms:.2f}")
            out.append(f"CH{ch + 1} range {rng}: " + "; ".join(parts))
    for ch, cores in sorted(cal.channel_fit.items()):
        out.append(f"CH{ch + 1} channel model (zero = Z + slope x dcoffset): " + "; ".join(
            f"{c}: Z {v['Z']:.2f}, slope {v['slope'] if v['slope'] is not None else 'fw'}, over {v['n_ranges']} "
            f"ranges (spread {v['spread']:.2f})" for c, v in sorted(cores.items())))
    return out


def main(argv: list[str] | None = None) -> None:
    ap = argparse.ArgumentParser(prog="python -m fnirsi1013d.calibration", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--import", dest="imports", nargs="+", metavar="T8_JSON", help="lead's T8 files to add")
    ap.add_argument("--out", help="calibration file to create or extend")
    ap.add_argument("--unit", default="", help="unit name stored in a new file")
    ap.add_argument("--show", metavar="FILE", help="print the fit of a calibration file")
    a = ap.parse_args(argv)
    if a.imports:
        if not a.out:
            ap.error("--import needs --out")
        cal = UnitCalibration.load_or_new(a.out, a.unit)
        import_t8(a.imports, cal)
        print(f"{len(cal.points)} points -> {cal.save(a.out)}")
        print("\n".join(describe(cal)))
    if a.show:
        print("\n".join(describe(UnitCalibration.load(a.show))))


if __name__ == "__main__":
    main()
