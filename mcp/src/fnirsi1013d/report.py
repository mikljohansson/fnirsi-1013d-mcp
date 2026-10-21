"""Result presentation: a PNG plot and a self-contained HTML page for a measurement the tools saved.

Every acquisition tool saves its data as an ``.npz`` (arrays plus ``meta_json``): ``scope_capture`` /
``scope_read_capture`` (``*_volts`` per channel), ``scope_averaged_capture`` (``*_mean``, ``*_std``),
``scope_decay_fit`` (data, fit, residual), ``scope_capture_segments`` (one record per segment),
``scope_capture_and_fft`` / ``scope_spectrum`` (a spectrum) and ``scope_bode_sweep`` (host or device; its ``.json``
beside it carries the table and flags). ``build_report`` turns one of them into

- a PNG (matplotlib, an optional dependency: ``pip install "fnirsi1013d[plot]"``; without it there is no PNG and the
  page says so), and
- an HTML page with the plot embedded (base64), the result table, the caveats and the command that produced it: one
  file, no scripts, no external fonts or CDNs, readable offline and in light or dark mode.

The roadmap's rule (docs/plans/roadmap.md): advanced analysis lives in the MCP tools; the agent presents the data.
This module is that presentation, also usable as ``python -m fnirsi1013d.report <file.npz>``.
"""

from __future__ import annotations

import argparse
import base64
import datetime as _dt
import html
import io
import json
import math
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable

import numpy as np

from . import analysis as A
from .paths import user_home

#: what each kind is called and which tool makes it
KINDS = {
    "capture": ("Capture", "scope_capture"),
    "averaged_capture": ("Averaged capture", "scope_averaged_capture"),
    "decay_fit": ("Decay fit", "scope_decay_fit"),
    "segments": ("Segmented capture", "scope_capture_segments"),
    "fft": ("Spectrum (host FFT)", "scope_capture_and_fft"),
    "spectrum": ("Spectrum (the scope's view)", "scope_spectrum"),
    "bode": ("Frequency response", "scope_bode_sweep"),
    "decode": ("Serial decode", "scope_decode"),
    "mask": ("Mask", "scope_mask_test"),
    "mask_test": ("Mask test", "scope_mask_test"),
    "dvm": ("DVM readings", "scope_dvm"),
    "peak": ("Peak-detect capture", "scope_capture"),
    "counter": ("Hardware counter", "scope_counter"),
    "log": ("Streaming log", "scope_log"),
    "pattern": ("Pulse pattern", "scope_pattern"),
}
#: series colours (validated categorical palette slots 1-2; channel identity is also named in the legend)
COLOURS = ("#2a78d6", "#eb6834", "#1baf7a", "#6250d6")
GREY = "#8a8984"


class ReportError(Exception):
    pass


@dataclass
class Result:
    """A saved result: its kind, arrays and metadata (``meta_json`` of the ``.npz``)."""

    path: Path
    kind: str
    arrays: dict[str, np.ndarray]
    meta: dict[str, Any]
    #: the Bode sweep's ``.json`` beside the ``.npz`` (table with flags, summary, caveats), when present
    sidecar: dict[str, Any] | None = None

    @property
    def title(self) -> str:
        return KINDS.get(self.kind, (self.kind, ""))[0]

    @property
    def tool(self) -> str:
        return KINDS.get(self.kind, ("", "the scope tools"))[1]


@dataclass
class Report:
    kind: str
    title: str
    html: str
    png: str | None
    rows: list[dict[str, Any]]
    caveats: list[str]
    summary: list[str] = field(default_factory=list)
    plot_note: str | None = None


def detect_kind(arrays: dict[str, np.ndarray], meta: dict[str, Any]) -> str:
    k = meta.get("kind")
    if k in KINDS:
        return str(k)
    if "mag_db" in arrays and "f_actual" in arrays:
        return "bode"
    if "t" in arrays and any(n.endswith("_volts") and arrays[n].ndim == 1 for n in arrays):
        return "capture"
    raise ReportError(f"unknown result (arrays {sorted(arrays)[:8]}): not a file saved by the scope tools")


def _num(x: Any) -> float:
    """A table number; None (a point without H, e.g. a dead CH1) is NaN."""
    return math.nan if x is None else float(x)


def load_result(path: str | Path) -> Result:
    """Load an ``.npz`` written by the server (``allow_pickle`` off: safe for files of unknown origin)."""
    p = Path(path)
    if not p.is_file():
        raise ReportError(f"{p}: no such file")
    if p.suffix.lower() == ".json":
        return _load_bode_json(p)
    try:
        with np.load(p, allow_pickle=False) as z:
            arrays = {k: z[k] for k in z.files}
    except (OSError, ValueError) as e:
        raise ReportError(f"{p}: not a readable .npz ({e})") from None
    meta: dict[str, Any] = {}
    if "meta_json" in arrays:
        try:
            meta = json.loads(str(arrays.pop("meta_json")))
        except ValueError:
            meta = {}
    kind = detect_kind(arrays, meta)
    side = None
    js = Path(str(meta.get("json") or p.with_suffix(".json")))
    if kind == "bode" and js.is_file():
        try:
            side = json.loads(js.read_text(encoding="utf-8"))
        except ValueError:
            side = None
    return Result(p, kind, arrays, meta, side)


def _load_bode_json(p: Path) -> Result:
    """A Bode sweep's ``.json`` (scope_bode_sweep files.json, fnirsi1013d-bode --json): the table carries everything."""
    try:
        d = json.loads(p.read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        raise ReportError(f"{p}: not a readable JSON file ({e})") from None
    table = d.get("table") if isinstance(d, dict) else None
    if not table or "f_actual" not in table[0]:
        raise ReportError(f"{p}: not a Bode sweep result (no table with f_actual)")
    from .bode import BAD_FLAGS

    arrays = {"f_actual": np.array([float(t["f_actual"]) for t in table]),
              "mag_db": np.array([_num(t["mag_db"]) for t in table]),
              "phase_deg": np.array([_num(t["phase_deg"]) for t in table]),
              "valid": np.array([not set(t.get("flags") or []) & BAD_FLAGS for t in table])}
    return Result(p, "bode", arrays, d, d)


def latest_result(data_dir: str | Path) -> Path:
    """The newest ``.npz`` under the server's data directory (its dated sub-directories)."""
    d = Path(data_dir)
    files = [p for p in d.glob("*/*.npz") if p.is_file()]
    if not files:
        raise ReportError(f"no saved results under {d}")
    return max(files, key=lambda q: q.stat().st_mtime)


# ---------------------------------------------------------------------------------------------------------
# Numbers


def _si(v: float | None, unit: str = "", digits: int = 4) -> str:
    if v is None or not isinstance(v, (int, float)) or not math.isfinite(v):
        return "-"
    if v == 0:
        return f"0 {unit}".strip()
    exp = int(math.floor(math.log10(abs(v)) / 3) * 3)
    exp = max(-12, min(9, exp))
    prefix = {-12: "p", -9: "n", -6: "u", -3: "m", 0: "", 3: "k", 6: "M", 9: "G"}[exp]
    return f"{v / 10 ** exp:.{digits}g} {prefix}{unit}".strip()


def _time_axis(t: np.ndarray) -> tuple[np.ndarray, str]:
    span = float(np.nanmax(np.abs(t))) if t.size else 1.0
    for scale, unit in ((1e-9, "ns"), (1e-6, "us"), (1e-3, "ms"), (1.0, "s")):
        if span < 1000 * scale:
            return t / scale, unit
    return t, "s"


def _channels(arrays: dict[str, np.ndarray], suffix: str) -> list[str]:
    return sorted(n[: -len(suffix)] for n in arrays if n.endswith(suffix) and n[: -len(suffix)] in ("CH1", "CH2"))


def _stats_row(name: str, v: np.ndarray, dt: float | None, clipped: np.ndarray | None = None) -> dict[str, Any]:
    st = A.stats(np.asarray(v, np.float64))
    row = {"channel": name, "mean": _si(st.get("mean"), "V"), "rms": _si(st.get("rms"), "V"),
           "min": _si(st.get("min"), "V"), "max": _si(st.get("max"), "V"), "Vpp": _si(st.get("vpp"), "V")}
    if dt:
        fr = A.frequency(np.asarray(v, np.float64), dt)
        row["frequency"] = _si(fr.get("frequency"), "Hz")
    if clipped is not None:
        row["clipped samples"] = int(np.asarray(clipped).sum())
    return row


def _rows(r: Result) -> tuple[list[dict[str, Any]], list[str]]:
    """The result table and summary lines per kind."""
    a, m = r.arrays, r.meta
    dt = m.get("dt")
    summ: list[str] = []
    if r.kind in ("capture", "peak"):
        rows = [_stats_row(c, a[f"{c}_volts"], dt, a.get(f"{c}_clipped")) for c in _channels(a, "_volts")]
        if r.kind == "peak":
            for row in rows:
                c = row["channel"]
                if f"{c}_min_volts" in a and f"{c}_max_volts" in a:
                    lo, hi = np.asarray(a[f"{c}_min_volts"], np.float64), np.asarray(a[f"{c}_max_volts"], np.float64)
                    row["envelope min"] = _si(float(lo.min()), "V")
                    row["envelope max"] = _si(float(hi.max()), "V")
                    row["mean width"] = _si(float(np.mean(hi - lo)), "V")
            d = m.get("decimation") or {}
            summ.append(f"peak detect: each sample is the min and max of {d.get('n', '?')} raw 200 MSa/s samples; "
                        "the table's mean / rms are of the midpoint")
        if dt:
            summ.append(f"{a['t'].size} samples at {_si(1 / dt, 'Sa/s')}, capture {m.get('capture_id', '?')}"
                        f"{', triggered' if m.get('triggered') else ', untriggered (auto)'}")
        return rows, summ
    if r.kind == "counter":
        f = np.asarray(a.get("frequency_hz", []), np.float64)
        rows = []
        for i in range(f.size):
            row = {"reading": i + 1, "frequency": _si(float(f[i]), "Hz", 7),
                   "period": _si(float(a["period_s"][i]), "s", 6), "+width": _si(float(a["width_s"][i]), "s", 5),
                   "duty": f"{100 * float(a['duty'][i]):.3f} %" if math.isfinite(float(a["duty"][i])) else "-"}
            if "vmin" in a:
                row["Vmin"] = _si(float(a["vmin"][i]), "V")
                row["Vmax"] = _si(float(a["vmax"][i]), "V")
            rows.append(row)
        summ.append(f"{m.get('source', '?')} over a {_si(m.get('gate_s'), 's')} gate, {f.size} reading(s); "
                    f"{m.get('resolution', '')}")
        if f.size > 1 and np.isfinite(f).any():
            summ.append(f"frequency mean {_si(float(np.nanmean(f)), 'Hz', 7)}, spread "
                        f"{_si(float(np.nanmax(f) - np.nanmin(f)), 'Hz', 3)}")
        return rows, summ
    if r.kind == "averaged_capture":
        rows = [_stats_row(c, a[f"{c}_mean"], dt) for c in _channels(a, "_mean")]
        summ.append(f"{m.get('n_used', '?')} captures averaged ({m.get('mode', 'host')}), {m.get('n_dropped', 0)} "
                    "dropped")
        return rows, summ
    if r.kind == "decay_fit":
        fit = m.get("fit") or {}
        params = fit.get("params") or {}
        rows = []
        for k, v in params.items():
            unit = "s" if k.startswith("tau") else "V" if k in ("amplitude", "amplitude2", "offset") else ""
            err = (fit.get("errors") or fit.get("stderr") or {}).get(k)
            rows.append({"parameter": k, "value": _si(v, unit, 5), "std error": _si(err, unit, 2)})
        for k in ("model", "r2", "rmse", "chi2_red"):
            if k in fit:
                summ.append(f"{k}: {fit[k] if not isinstance(fit[k], float) else round(fit[k], 6)}")
        summ.append(f"{m.get('n_used', '?')} captures averaged before the fit")
        return rows, summ
    if r.kind == "segments":
        ts = a.get("timestamps_s")
        trig = a.get("triggered")
        slots = a.get("slots")
        n = 0
        for c in _channels(a, "_volts"):
            n = a[f"{c}_volts"].shape[0]
        rows = []
        for i in range(min(n, 50)):
            row: dict[str, Any] = {"segment": int(slots[i]) if slots is not None else i}
            if ts is not None and i < len(ts):
                row["t"] = _si(float(ts[i]), "s", 6)
            if trig is not None and i < len(trig):
                row["triggered"] = bool(trig[i])
            for c in _channels(a, "_volts"):
                v = a[f"{c}_volts"][i]
                row[f"{c} Vpp"] = _si(float(np.ptp(v)), "V")
                row[f"{c} mean"] = _si(float(np.mean(v)), "V")
            rows.append(row)
        summ.append(f"{n} segments" + (" (table: the first 50)" if n > 50 else ""))
        return rows, summ
    if r.kind in ("fft", "spectrum"):
        f = a["f"]
        if r.kind == "fft":
            amp = a["amplitude_vrms"]
            unit = "dBV"
        else:
            lv = a["level_db"]
            amp = np.where(np.isnan(lv), 0.0, 10 ** (np.nan_to_num(lv, nan=-400.0) / 20))
            unit = str(m.get("unit", "dBV"))
        floor = float(np.median(amp[1:])) if amp.size > 2 else 0.0
        pk = A.peaks(f, amp, 8, float(f[1]) if f.size > 1 else 0.0, float(f[-1]), floor=floor * 3)
        ref = pk[0][1] if pk else 1.0
        rows = [{"frequency": _si(fr, "Hz", 6), unit: round(A.db(v), 2), "dBc": round(A.db(v, ref), 2)}
                for fr, v, _ in pk]
        if floor > 0:
            summ.append(f"median level per bin {A.db(floor):.1f} {unit}")
        return rows, summ
    if r.kind == "bode":
        table = (r.sidecar or {}).get("table")
        rows = []
        if table:
            for t in table:
                rows.append({"f": _si(t.get("f_actual"), "Hz", 6), "|H| dB": t.get("mag_db"),
                             "phase deg": t.get("phase_deg"), "CH2 V/div": t.get("ch2_volts_per_div"),
                             "flags": " ".join(t.get("flags") or [])})
        else:
            for f, mg, ph, ok in zip(a["f_actual"], a["mag_db"], a["phase_deg"], a.get("valid", [])):
                rows.append({"f": _si(float(f), "Hz", 6), "|H| dB": round(float(mg), 3),
                             "phase deg": round(float(ph), 2), "valid": bool(ok)})
        sm = (r.sidecar or {}).get("summary") or m.get("summary") or {}
        if sm.get("shape"):
            summ.append(f"shape: {sm['shape']}")
        for f3 in sm.get("minus_3db_hz") or []:
            summ.append(f"-3 dB at {_si(f3, 'Hz', 5)} (re {sm.get('reference_db', 0):.2f} dB)")
        pk = sm.get("peak")
        if pk:
            summ.append(f"peak {pk['mag_db']:.2f} dB at {_si(pk['f_hz'], 'Hz', 5)}" +
                        (f", Q {pk['q']:.3g}" if "q" in pk else ""))
        for k, v in (sm.get("phase_crossings_hz") or {}).items():
            summ.append(f"phase {k} deg at {_si(v, 'Hz', 5)}")
        where = (r.sidecar or m).get("meta", {}).get("where") or m.get("meta", {}).get("where")
        if where == "device":
            summ.append("measured by the scope's own Bode engine (where='device')")
        return rows, summ
    if r.kind == "decode":
        frames = m.get("frames") or []
        proto = m.get("protocol", "?")
        rows = []
        for f in frames[:200]:
            row: dict[str, Any] = {"t": _si(f.get("t"), "s", 6)}
            if proto == "i2c":
                row.update({"address": f.get("address"), "rw": f.get("rw"), "ack": f.get("ack"),
                            "data": " ".join(f"{d['hex']}{'' if d['ack'] else '!'}" for d in f.get("data") or []),
                            "stop": "P" if f.get("stop") else ("Sr" if f.get("repeated_start") else "")})
            else:
                row.update({"value": f.get("hex"), "char": f.get("char", "")})
                if proto == "spi":
                    row["transfer"] = f.get("transfer")
            row["errors"] = ", ".join(f.get("errors") or [])
            rows.append(row)
        ap = m.get("applied") or {}
        what = {"uart": f"{ap.get('baud', 0):.6g} baud, {ap.get('data_bits')} data bits, parity {ap.get('parity')}, "
                        f"{ap.get('samples_per_bit', 0):.1f} samples per bit",
                "spi": f"clock {_si(ap.get('clock_hz'), 'Hz')}, {ap.get('word_bits')} bits per word, CPOL {ap.get('cpol')} "
                       f"CPHA {ap.get('cpha')}, {ap.get('samples_per_clock', 0):.1f} samples per clock",
                "i2c": f"SCL {_si(ap.get('scl_hz'), 'Hz')}, {ap.get('samples_per_scl_period', 0):.1f} samples per period"
                }.get(proto, "")
        summ.append(f"{proto.upper()}: {len(frames)} frame(s), {m.get('n_errors', 0)} with errors; {what}"
                    + (" (table: the first 200)" if len(frames) > 200 else ""))
        return rows, summ
    if r.kind in ("mask", "mask_test"):
        rows = []
        for res in m.get("results") or []:
            row = {"capture": res.get("capture_id"), "passed": res.get("passed"), "fail samples": res.get("fail_samples")}
            ff = res.get("first_fail")
            if ff:
                row["first fail at"] = _si(ff.get("t"), "s", 6)
                row["volts"] = _si(ff.get("volts"), "V")
                row["bound"] = f"{ff.get('bound')} {_si(ff.get('limit'), 'V')}"
            rows.append(row)
        tol = m.get("tolerance") or {}
        summ.append(f"mask: +-{_si(tol.get('volts'), 'V')} and +-{tol.get('samples', '?')} sample(s) "
                    f"({_si(tol.get('time_s'), 's')}) around the reference on {m.get('channel', '?')}")
        if r.kind == "mask_test":
            summ.append(f"{m.get('passed', 0)} passed, {m.get('failed', 0)} failed of {m.get('n_tested', 0)} captures"
                        + (f", {m['dropped']} dropped" if m.get("dropped") else ""))
        return rows, summ
    if r.kind == "log":
        rows = []
        for c in m.get("channels") or []:
            lo, hi = np.asarray(a[f"{c}_min_volts"], np.float64), np.asarray(a[f"{c}_max_volts"], np.float64)
            me = np.asarray(a[f"{c}_mean_volts"], np.float64)
            rows.append({"channel": c, "min": _si(float(np.nanmin(lo)), "V") if lo.size else "-",
                         "max": _si(float(np.nanmax(hi)), "V") if hi.size else "-",
                         "mean": _si(float(np.nanmean(me)), "V") if me.size else "-"})
        rate = float(m.get("sample_rate") or 0)
        n = int(m.get("samples") or 0)
        summ.append(f"{n} samples per channel at {_si(rate, 'Sa/s')} ({m.get('format')}), "
                    f"{_si(n / rate if rate else 0.0, 's')} of data over {_si(m.get('elapsed_s') or 0.0, 's')}, "
                    f"state {m.get('state')}")
        summ.append(f"{m.get('gaps', 0)} gap(s), {m.get('lost_samples', 0)} samples per channel lost; the plot shows "
                    f"the min .. max of {m.get('samples_per_bin', '?')} samples per point; raw files in "
                    f"{m.get('directory')}")
        return rows, summ
    if r.kind == "pattern":
        p = m.get("pattern") or {}
        rows = [{"step": i, "level": st.get("level"), "duration": _si(st.get("seconds"), "s", 7),
                 "clocks": f"{st.get('clocks')} + {st.get('fraction')}/65536", "marker": "yes" if st.get("mark")
                 else ""} for i, st in enumerate(p.get("steps") or [])]
        summ.append(f"mode {p.get('mode')}, repeat {p.get('repeat')}, gap {_si((p.get('gap_ns') or 0) * 1e-9, 's')}, "
                    f"burst {_si(p.get('burst_s'), 's', 7)}, start {p.get('start_source')}")
        return rows, summ
    if r.kind == "dvm":
        rows = []
        for c, s in (m.get("readings") or {}).items():
            res = s.get("resolution") or {}
            rows.append({"channel": c, "DC": _si(s.get("dc_mean"), "V", 5), "AC rms": _si(s.get("ac_rms"), "V"),
                         "true rms": _si(s.get("rms"), "V"), "min": _si(s.get("min"), "V"), "max": _si(s.get("max"), "V"),
                         "Vpp": _si(s.get("vpp"), "V"), "frequency": _si(s.get("frequency"), "Hz"),
                         "+-1 sigma": _si(res.get("uncertainty_volts"), "V", 2),
                         "digits": round(res["digits"], 1) if res.get("digits") is not None else None})
        summ.append(f"{m.get('n_captures', '?')} captures, {m.get('n_samples_total', '?')} samples in all over "
                    f"{m.get('elapsed_s', 0):.1f} s")
        return rows, summ
    return [], summ


def _caveats(r: Result, extra: Iterable[str] | None) -> list[str]:
    out: list[str] = []
    for src in (r.meta.get("caveats"), (r.sidecar or {}).get("caveats")):
        for c in src or []:
            if c not in out:
                out.append(str(c))
    if r.meta.get("scaling") and r.meta["scaling"] not in out:
        out.append(str(r.meta["scaling"]))
    for c in extra or []:
        if c and c not in out:
            out.append(str(c))
    return out


def _command(r: Result) -> str:
    args = r.meta.get("args")
    if r.kind == "bode" and r.sidecar is not None:
        args = args or r.sidecar.get("args")
    if isinstance(args, dict) and args:
        body = ", ".join(f"{k}={v!r}" for k, v in args.items() if v is not None and k != "confirm_token")
        return f"{r.tool}({body})"
    return f"{r.tool}(...)  # arguments not recorded in {r.path.name}"


# ---------------------------------------------------------------------------------------------------------
# Plot


def have_matplotlib() -> bool:
    try:
        import matplotlib  # noqa: F401
    except ImportError:
        return False
    return True


def _plot(r: Result, title: str) -> bytes:
    import matplotlib

    matplotlib.use("Agg", force=True)
    import matplotlib.pyplot as plt

    a, m = r.arrays, r.meta
    plt.rcParams.update({"font.size": 9, "axes.edgecolor": "#b7b6af", "axes.labelcolor": "#33332f",
                         "xtick.color": "#52514e", "ytick.color": "#52514e", "axes.spines.top": False,
                         "axes.spines.right": False, "grid.color": "#e4e3dd", "grid.linewidth": 0.6,
                         "lines.linewidth": 1.4})
    two = r.kind in ("decay_fit", "bode")
    fig, axes = plt.subplots(2 if two else 1, 1, figsize=(8.0, 5.2 if two else 3.8), sharex=two,
                             gridspec_kw={"height_ratios": [3, 1.4] if r.kind == "decay_fit" else [1, 1]} if two
                             else None)
    ax = axes[0] if two else axes
    ax.set_title(title, loc="left", fontsize=10.5, color="#0b0b0b")
    if r.kind in ("capture", "peak", "averaged_capture", "segments", "decode", "mask", "mask_test"):
        t, unit = _time_axis(np.asarray(a["t"], np.float64))
        suffix = {"capture": "_volts", "peak": "_volts", "averaged_capture": "_mean", "segments": "_volts",
                  "decode": "_volts", "mask": "_volts", "mask_test": "_volts"}[r.kind]
        if r.kind in ("mask", "mask_test"):
            up, lo = np.asarray(a["upper"], np.float64), np.asarray(a["lower"], np.float64)
            ax.fill_between(t[: up.size], lo, up, color=COLOURS[2], alpha=0.22, linewidth=0, label="mask")
            if "reference" in a and r.kind == "mask":
                ax.plot(t[: up.size], a["reference"], color=GREY, linewidth=0.9, label="reference")
            fm = a.get("fail_mask")
            if fm is not None and np.any(fm):
                for c in _channels(a, suffix):
                    idx = np.flatnonzero(fm)[:2000]
                    ax.plot(t[idx], a[f"{c}{suffix}"][idx], "x", color="#e34948", markersize=4, label="outside the mask")
        if r.kind == "decode":
            raw_t = np.asarray(a["t"], np.float64)
            scale = float(t[-1] / raw_t[-1]) if raw_t.size and raw_t[-1] else 1.0
            for f in (m.get("frames") or [])[:120]:
                tf = float(f.get("t", 0.0)) * scale
                label = f.get("hex") if m.get("protocol") != "i2c" else f"S {f.get('address')} {f.get('rw', '')[:1].upper()}"
                ax.axvline(tf, color=GREY, linewidth=0.6, alpha=0.6)
                ax.annotate(label, (tf, 1.0), xycoords=("data", "axes fraction"), xytext=(2, -10),
                            textcoords="offset points", fontsize=7, color="#33332f", rotation=90, va="top")
        for i, c in enumerate(_channels(a, suffix)):
            col = COLOURS[i]
            v = a[f"{c}{suffix}"]
            if r.kind == "segments":
                n = v.shape[0]
                tt = t[: v.shape[1]] if t.size >= v.shape[1] else np.arange(v.shape[1]) * (m.get("dt") or 1)
                for k in range(min(n, 64)):
                    ax.plot(tt, v[k], color=col, alpha=max(0.08, 1.0 / min(n, 64) ** 0.5), linewidth=0.9,
                            label=f"{c} ({n} segments)" if k == 0 else None)
                continue
            if r.kind == "peak" and f"{c}_min_volts" in a and f"{c}_max_volts" in a:
                lo, hi = a[f"{c}_min_volts"], a[f"{c}_max_volts"]
                ax.fill_between(t[: v.size], lo, hi, color=col, alpha=0.35, linewidth=0, label=f"{c} min .. max")
                ax.plot(t[: v.size], hi, color=col, linewidth=0.6)
                ax.plot(t[: v.size], lo, color=col, linewidth=0.6)
                continue
            ax.plot(t[: v.size], v, color=col, label=c)
            std = a.get(f"{c}_std")
            if std is not None:
                ax.fill_between(t[: v.size], v - std, v + std, color=col, alpha=0.18, linewidth=0,
                                label=f"{c} +-1 std of one capture")
            cl = a.get(f"{c}_clipped")
            if cl is not None and np.any(cl):
                idx = np.flatnonzero(cl)[:2000]
                ax.plot(t[idx], v[idx], "x", color="#e34948", markersize=4, label=f"{c} clipped")
        ax.set_xlabel(f"time ({unit})")
        ax.set_ylabel("volts at the probe tip")
    elif r.kind == "log":
        t, unit = _time_axis(np.asarray(a["t"], np.float64))
        scale = float(t[-1] / a["t"][-1]) if a["t"].size and a["t"][-1] else 1.0
        for i, c in enumerate(m.get("channels") or []):
            col = COLOURS[i]
            ax.fill_between(t, a[f"{c}_min_volts"], a[f"{c}_max_volts"], color=col, alpha=0.3, linewidth=0,
                            label=f"{c} min .. max")
            ax.plot(t, a[f"{c}_mean_volts"], color=col, linewidth=0.8, label=f"{c} mean")
        for k, tg in enumerate(np.asarray(a.get("gap_t", []), np.float64)):
            ax.axvline(tg * scale, color="#e34948", linewidth=0.9, linestyle="--", label="gap" if k == 0 else None)
        ax.set_xlabel(f"time since the start ({unit})")
        ax.set_ylabel("volts at the probe tip")
    elif r.kind == "pattern":
        t, unit = _time_axis(np.asarray(a["t"], np.float64))
        lv = np.asarray(a["level"], np.float64)
        ax.step(t, np.append(lv, lv[-1]) if lv.size == t.size - 1 else lv, where="post", color=COLOURS[0],
                label="pin level")
        scale = float(t[-1] / a["t"][-1]) if a["t"].size and a["t"][-1] else 1.0
        for k, tm in enumerate(np.asarray(a.get("marks", []), np.float64)):
            ax.axvline(tm * scale, color=COLOURS[1], linewidth=0.9, linestyle=":", label="marker" if k == 0 else None)
        ax.set_ylim(-0.2, 1.3)
        ax.set_yticks([0, 1])
        ax.set_xlabel(f"time from the start ({unit})")
        ax.set_ylabel("logic level")
    elif r.kind == "counter":
        tc = np.asarray(a["capture_t_s"], np.float64)
        f = np.asarray(a["frequency_hz"], np.float64)
        ax.plot(tc, f, "o-", color=COLOURS[0], markersize=4, linewidth=0.9, label=f"{m.get('source', '')} frequency")
        if "vmin" in a:
            ax2 = ax.twinx()
            ax2.fill_between(tc, a["vmin"], a["vmax"], color=COLOURS[1], alpha=0.15, linewidth=0, label="Vmin .. Vmax")
            ax2.set_ylabel("volts at the probe tip")
            ax2.spines["right"].set_visible(True)
        ax.set_xlabel("gate time (s since the first reading)")
        ax.set_ylabel("frequency (Hz)")
    elif r.kind == "dvm":
        tc = np.asarray(a["capture_t_s"], np.float64)
        for i, c in enumerate(_channels(a, "_mean")):
            col = COLOURS[i]
            mean = np.asarray(a[f"{c}_mean"], np.float64)
            ax.plot(tc, mean, "o-", color=col, markersize=3, linewidth=0.9, label=f"{c} DC per capture")
            if f"{c}_min" in a and f"{c}_max" in a:
                ax.fill_between(tc, a[f"{c}_min"], a[f"{c}_max"], color=col, alpha=0.12, linewidth=0,
                                label=f"{c} min .. max")
        ax.set_xlabel("time since the first capture (s)")
        ax.set_ylabel("volts at the probe tip")
    elif r.kind == "decay_fit":
        t, unit = _time_axis(np.asarray(a["t"], np.float64))
        ax.plot(t, a["data"], color=COLOURS[0], label="data")
        ax.plot(t, a["fit"], color=COLOURS[1], label="fit")
        ax.set_ylabel("volts")
        ax2 = axes[1]
        ax2.plot(t, a["residual"], color=GREY, linewidth=0.9)
        ax2.axhline(0, color="#b7b6af", linewidth=0.8)
        ax2.set_ylabel("residual (V)")
        ax2.set_xlabel(f"time ({unit})")
        ax2.grid(True)
    elif r.kind in ("fft", "spectrum"):
        f = np.asarray(a["f"], np.float64)
        if r.kind == "fft":
            y = 20 * np.log10(np.maximum(np.asarray(a["amplitude_vrms"], np.float64), 1e-12))
            yl = "dBV (RMS per bin)"
        else:
            y = np.asarray(a["level_db"], np.float64)
            yl = f"{m.get('unit', 'dBV')} per bin"
        sel = f > 0
        ax.plot(f[sel], y[sel], color=COLOURS[0], linewidth=1.0)
        if f[sel].size and f[sel][-1] / f[sel][0] > 300:
            ax.set_xscale("log")
        ax.set_xlabel("frequency (Hz)")
        ax.set_ylabel(yl)
    elif r.kind == "bode":
        f = np.asarray(a["f_actual"], np.float64)
        mag, ph = np.asarray(a["mag_db"], np.float64), np.asarray(a["phase_deg"], np.float64)
        ok = np.asarray(a.get("valid", np.ones(f.size, bool)), bool)
        ph_u = np.degrees(np.unwrap(np.radians(ph))) if ph.size else ph
        for axx, y, lab in ((ax, mag, "|H| (dB)"), (axes[1], ph_u, "phase (deg)")):
            # the line joins valid points only, as on the scope (a flagged point is a loose grey dot; fw 0.2.99:
            # ch2_small too, so a pickup tail under the floor no longer reads as the curve)
            axx.plot(f, np.where(ok, y, np.nan), color=COLOURS[0], linewidth=1.2, zorder=2)
            axx.plot(f[ok], y[ok], "o", color=COLOURS[0], markersize=4, zorder=3,
                     label="valid" if axx is ax else None)
            if np.any(~ok):
                axx.plot(f[~ok], y[~ok], "o", markerfacecolor="white", markeredgecolor=GREY, markersize=5, zorder=3,
                         label="flagged (left out of the markers)" if axx is ax else None)
            axx.set_ylabel(lab)
            axx.set_xscale("log")
            axx.grid(True, which="both")
        sm = (r.sidecar or {}).get("summary") or m.get("summary") or {}
        for f3 in sm.get("minus_3db_hz") or []:
            ax.axvline(f3, color=COLOURS[1], linewidth=0.9, linestyle="--")
            ax.annotate(f"-3 dB {_si(f3, 'Hz', 4)}", (f3, ax.get_ylim()[0]), xytext=(4, 6),
                        textcoords="offset points", color="#33332f", fontsize=8)
        axes[1].set_xlabel("frequency (Hz)")
    ax.grid(True, which="both" if r.kind in ("bode",) else "major")
    handles, labels = ax.get_legend_handles_labels()
    if len(labels) > 1 or r.kind == "bode" and labels:
        ax.legend(frameon=False, fontsize=8, loc="best")
    fig.tight_layout()
    buf = io.BytesIO()
    fig.savefig(buf, format="png", dpi=120, facecolor="white")
    plt.close(fig)
    return buf.getvalue()


# ---------------------------------------------------------------------------------------------------------
# HTML

_CSS = """
:root{--bg:#fcfcfb;--fg:#0b0b0b;--muted:#52514e;--line:#e4e3dd;--card:#ffffff;--warn:#8a5a00;--warnbg:#fff6e0;
--code:#f3f2ee}
@media (prefers-color-scheme: dark){:root:not([data-theme="light"]){--bg:#1a1a19;--fg:#f0efec;--muted:#c3c2b7;
--line:#383835;--card:#222221;--warn:#f3c969;--warnbg:#3a3220;--code:#2a2a28}}
:root[data-theme="dark"]{--bg:#1a1a19;--fg:#f0efec;--muted:#c3c2b7;--line:#383835;--card:#222221;--warn:#f3c969;
--warnbg:#3a3220;--code:#2a2a28}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);
font:15px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
main{max-width:960px;margin:0 auto;padding:24px 16px 48px}
h1{font-size:22px;margin:0 0 4px}h2{font-size:16px;margin:28px 0 8px}
.sub{color:var(--muted);font-size:13px;margin:0 0 16px}
figure{margin:0;background:#fff;border:1px solid var(--line);border-radius:8px;padding:8px}
figure img{display:block;width:100%;height:auto}
.note{color:var(--muted);font-style:italic}
ul.sum{margin:8px 0 0;padding-left:20px}
.tw{overflow-x:auto;border:1px solid var(--line);border-radius:8px;background:var(--card)}
table{border-collapse:collapse;width:100%;font-size:13px;font-variant-numeric:tabular-nums}
th,td{padding:6px 10px;border-bottom:1px solid var(--line);text-align:right;white-space:nowrap}
th{color:var(--muted);font-weight:600;position:sticky;top:0;background:var(--card)}
td:first-child,th:first-child{text-align:left}tr:last-child td{border-bottom:0}
.cav{background:var(--warnbg);border-left:3px solid var(--warn);padding:10px 14px;border-radius:4px}
.cav li{margin:4px 0}
pre{background:var(--code);padding:10px 12px;border-radius:6px;overflow-x:auto;font-size:12.5px;white-space:pre-wrap;
word-break:break-word}
details{margin-top:8px}summary{cursor:pointer;color:var(--muted)}
"""


def _table_html(rows: list[dict[str, Any]]) -> str:
    if not rows:
        return '<p class="note">no table for this result</p>'
    cols: list[str] = []
    for r in rows:
        for k in r:
            if k not in cols:
                cols.append(k)
    head = "".join(f"<th>{html.escape(str(c))}</th>" for c in cols)
    body = "".join("<tr>" + "".join(f"<td>{html.escape('' if r.get(c) is None else str(r.get(c)))}</td>"
                                   for c in cols) + "</tr>" for r in rows)
    return f'<div class="tw"><table><thead><tr>{head}</tr></thead><tbody>{body}</tbody></table></div>'


def _html(title: str, sub: str, png: bytes | None, plot_note: str | None, summary: list[str],
          rows: list[dict[str, Any]], caveats: list[str], command: str, meta: dict[str, Any]) -> str:
    img = (f'<figure><img alt="{html.escape(title)}" src="data:image/png;base64,{base64.b64encode(png).decode()}">'
           "</figure>") if png else f'<p class="note">{html.escape(plot_note or "no plot")}</p>'
    summ = ("<ul class=\"sum\">" + "".join(f"<li>{html.escape(s)}</li>" for s in summary) + "</ul>") if summary else ""
    cav = ('<div class="cav"><ul>' + "".join(f"<li>{html.escape(c)}</li>" for c in caveats) + "</ul></div>") \
        if caveats else '<p class="note">no caveats recorded</p>'
    meta_txt = html.escape(json.dumps(meta, indent=1, default=str)[:20000])
    return (f'<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" '
            f'content="width=device-width,initial-scale=1"><title>{html.escape(title)}</title><style>{_CSS}</style>'
            f'</head><body><main><h1>{html.escape(title)}</h1><p class="sub">{html.escape(sub)}</p>{img}{summ}'
            f"<h2>Results</h2>{_table_html(rows)}<h2>Caveats</h2>{cav}<h2>How this was measured</h2>"
            f"<pre>{html.escape(command)}</pre><details><summary>All recorded settings</summary><pre>{meta_txt}</pre>"
            "</details></main></body></html>")


def build_report(source: str | Path, *, title: str | None = None, command: str | None = None,
                 caveats: Iterable[str] | None = None, out_dir: str | Path | None = None,
                 plot: bool = True) -> dict[str, Any]:
    """Write ``<stem>.png`` (when matplotlib is installed) and ``<stem>.html`` for a saved result into ``out_dir``
    (default ``<per-user home>/reports/<date>``). Returns the paths, the kind, the table rows, the summary lines and
    the caveats."""
    r = load_result(source)
    title = title or f"{r.title}: {r.path.stem}"
    out = Path(out_dir) if out_dir is not None else user_home() / "reports" / _dt.date.today().isoformat()
    out.mkdir(parents=True, exist_ok=True)
    rows, summary = _rows(r)
    cav = _caveats(r, caveats)
    cmd = command or _command(r)
    png: bytes | None = None
    note = None
    if not plot:
        note = "plot not requested"
    elif not have_matplotlib():
        note = 'no plot: matplotlib is not installed (pip install "fnirsi1013d[plot]"); the numbers are below'
    else:
        png = _plot(r, title)
    stem = r.path.stem
    png_path = None
    if png is not None:
        png_path = out / f"{stem}.png"
        png_path.write_bytes(png)
    made = _dt.datetime.now().isoformat(timespec="seconds")
    sub = f"{r.title} from {r.path.name}, report made {made} by fnirsi1013d"
    page = _html(title, sub, png, note, summary, rows, cav, cmd, r.meta)
    html_path = out / f"{stem}.html"
    html_path.write_bytes(page.encode("utf-8"))
    return {"kind": r.kind, "title": title, "source": str(r.path), "png": str(png_path) if png_path else None,
            "html": str(html_path), "rows": rows, "summary": summary, "caveats": cav, "command": cmd,
            "plot_note": note}


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="python -m fnirsi1013d.report", description=(
        "PNG plot and self-contained HTML page for a result the fnirsi1013d tools saved (.npz)."))
    ap.add_argument("source", help=".npz (or the Bode .json beside it); 'latest' = the newest under --data-dir")
    ap.add_argument("--data-dir", default=str(user_home() / "data"))
    ap.add_argument("--out-dir", default=None)
    ap.add_argument("--title", default=None)
    ap.add_argument("--command", default=None, help="the command that produced the result (shown on the page)")
    ap.add_argument("--caveat", action="append", default=[], help="an extra caveat (repeatable)")
    a = ap.parse_args(argv)
    try:
        src = latest_result(a.data_dir) if a.source == "latest" else a.source
        res = build_report(src, title=a.title, command=a.command, caveats=a.caveat, out_dir=a.out_dir)
    except ReportError as e:
        print(f"report: {e}", file=sys.stderr)
        return 2
    print(f"html: {res['html']}")
    print(f"png:  {res['png'] or res['plot_note']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
