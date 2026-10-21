"""CSV export of a saved result (``scope_export_csv``): the arrays of an ``.npz`` the tools wrote as columns, with a
``#`` header carrying the settings, the scaling and the recorded caveats.

Columns: every 1-D array as long as the x axis (``t``, ``f``, ``f_actual`` or ``capture_t_s``), x first; booleans as
0 / 1, complex arrays as ``_re`` / ``_im``. A result with 2-D arrays (segments x samples) is written long: a leading
``segment`` column, the per-segment arrays (timestamps, triggered, slots) repeated on every row. Raw codes
(``*_codes``, ``*_raw_a`` / ``_raw_b``, ``*_samples16``) only with ``raw``.
"""

from __future__ import annotations

import datetime as _dt
from pathlib import Path
from typing import Any

import numpy as np

from . import protocol as P
from .report import Result, load_result

X_AXES = ("t", "f", "f_actual", "capture_t_s")
RAW_SUFFIXES = ("_codes", "_raw_a", "_raw_b", "_samples16")
UNITS = {"t": "s", "f": "Hz", "f_actual": "Hz", "capture_t_s": "s", "timestamps_s": "s", "mag_db": "dB",
         "phase_deg": "deg", "snr_db": "dB", "amplitude_vrms": "V rms", "level_db": "dB", "edge_shifts_samples":
         "samples", "upper": "V", "lower": "V", "reference": "V", "data": "V", "fit": "V", "residual": "V", "std": "V"}
SUFFIX_UNITS = (("_volts", "V"), ("_mean", "V"), ("_std", "V"), ("_ac_rms", "V"), ("_rms", "V"), ("_min", "V"),
                ("_max", "V"), ("_vpp", "V"), ("_frequency", "Hz"), ("_codes", "code"), ("_raw_a", "code"),
                ("_raw_b", "code"), ("_samples16", "count"), ("_clipped", "0/1"), ("_clipped_any", "0/1"),
                ("_mask", "0/1"))


def unit_of(name: str) -> str:
    if name in UNITS:
        return UNITS[name]
    for suf, u in SUFFIX_UNITS:
        if name.endswith(suf):
            return u
    return ""


def _channel_lines(meta: dict[str, Any]) -> list[str]:
    out = []
    for name, c in (meta.get("channels") or {}).items():
        if not isinstance(c, dict):
            continue
        rng = c.get("range")
        probe = P.PROBE_FACTORS[c["probe_index"]] if isinstance(c.get("probe_index"), int) and \
            c["probe_index"] < len(P.PROBE_FACTORS) else c.get("probe", 1.0)
        vdiv = P.RANGE_VOLTS_PER_DIV[rng] * probe if isinstance(rng, int) and rng < len(P.RANGE_VOLTS_PER_DIV) else None
        bits = [f"{vdiv:g} V/div" if vdiv is not None else f"range {rng}", "AC" if c.get("coupling") else "DC",
                f"probe x{probe:g}"]
        if c.get("invert"):
            bits.append("inverted")
        if "offset_pwm" in c:
            bits.append(f"offset_pwm {c['offset_pwm']}")
        out.append(f"# {name}: {', '.join(bits)}")
    return out


def header_lines(r: Result, columns: list[str], raw: bool) -> list[str]:
    m = r.meta
    lines = [f"# fnirsi1013d {r.title} export of {r.path.name}, made {_dt.datetime.now().isoformat(timespec='seconds')}"]
    args = m.get("args")
    if isinstance(args, dict) and args:
        lines.append(f"# tool: {r.tool}(" + ", ".join(f"{k}={v!r}" for k, v in args.items() if v is not None) + ")")
    else:
        lines.append(f"# tool: {r.tool}")
    acq = []
    if m.get("dt"):
        acq.append(f"sample_rate {1 / float(m['dt']):.10g} Sa/s (dt {float(m['dt']):.6g} s)")
    for k in ("capture_id", "divider", "pretrigger_samples", "total_samples", "n_used", "n_dropped", "mode"):
        if k in m and m[k] is not None:
            acq.append(f"{k} {m[k]}")
    if "triggered" in m:
        acq.append("triggered" if m["triggered"] else "untriggered (auto time-out)")
    if acq:
        lines.append("# acquisition: " + "; ".join(acq))
    lines += _channel_lines(m)
    tr = m.get("trigger")
    if isinstance(tr, dict):
        lines.append("# trigger: " + ", ".join(f"{k} {v}" for k, v in tr.items() if not k.startswith("_")))
    dec = m.get("decimation")
    if isinstance(dec, dict):
        lines.append("# decimation: " + ", ".join(f"{k} {v}" for k, v in dec.items()))
    if m.get("scaling"):
        lines.append(f"# scaling: {m['scaling']}")
    for c in m.get("caveats") or []:
        lines.append(f"# caveat: {c}")
    if raw:
        lines.append("# raw codes: *_codes are the compensated 8-bit codes per sample (fractional after decimation), "
                     "*_samples16 the stored 16-bit filter outputs; the per-core arrays *_raw_a / *_raw_b (half the "
                     "samples each) stay in the .npz")
    lines.append("# columns: " + ", ".join(f"{c} ({unit_of(c)})" if unit_of(c) else c for c in columns))
    return lines


def _as_columns(arrays: dict[str, np.ndarray], raw: bool) -> tuple[list[str], list[np.ndarray], int]:
    """(names, columns, rows) with the x axis first; 2-D arrays flattened to long format with a ``segment`` column."""
    x = next((k for k in X_AXES if k in arrays and arrays[k].ndim == 1), None)
    if x is None:
        raise ValueError(f"no time or frequency axis among {sorted(arrays)[:8]}")
    n = arrays[x].size
    two_d = {k: v for k, v in arrays.items() if v.ndim == 2 and v.shape[1] == n}
    names: list[str] = []
    cols: list[np.ndarray] = []

    def add(name: str, col: np.ndarray) -> None:
        if np.iscomplexobj(col):
            names.extend([f"{name}_re", f"{name}_im"])
            cols.extend([col.real.astype(np.float64), col.imag.astype(np.float64)])
        else:
            names.append(name)
            cols.append(col.astype(np.float64))

    if two_d:
        nseg = next(iter(two_d.values())).shape[0]
        seg_idx = np.repeat(np.arange(nseg), n)
        add("segment", seg_idx)
        add(x, np.tile(arrays[x], nseg))
        for k, v in two_d.items():
            add(k, v.reshape(-1))
        for k, v in arrays.items():
            if k != x and v.ndim == 1 and v.size == nseg:  # per-segment: timestamps, triggered, slots
                add(k, np.repeat(v, n))
        return names, cols, n * nseg
    add(x, arrays[x])
    for k, v in arrays.items():
        if k == x or v.ndim != 1 or v.size != n:
            continue
        if not raw and k.endswith(RAW_SUFFIXES):
            continue
        add(k, v)
    return names, cols, n


def write_csv(source: str | Path, out: str | Path | None = None, *, raw: bool = False, delimiter: str = ",",
              caveats: list[str] | None = None) -> dict[str, Any]:
    """Write ``source`` (an ``.npz`` a tool saved) as CSV to ``out`` (default: beside it with ``.csv``). Returns the
    path, the columns with units, the row count and the header lines."""
    r = load_result(source)
    arrays = {k: v for k, v in r.arrays.items() if isinstance(v, np.ndarray) and v.dtype.kind in "biufc"}
    names, cols, rows = _as_columns(arrays, raw)
    out_p = Path(out) if out is not None else r.path.with_suffix(".csv")
    if out_p.suffix.lower() != ".csv":
        out_p = out_p.with_suffix(".csv")
    out_p.parent.mkdir(parents=True, exist_ok=True)
    head = header_lines(r, names, raw)
    for c in caveats or []:
        head.insert(-1, f"# caveat: {c}")
    mat = np.column_stack(cols) if cols else np.empty((0, 0))
    with open(out_p, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(head) + "\n")
        fh.write(delimiter.join(names) + "\n")
        np.savetxt(fh, mat, delimiter=delimiter, fmt="%.9g")
    return {"file": str(out_p), "kind": r.kind, "source": str(r.path), "columns": names,
            "units": {c: unit_of(c) for c in names}, "rows": rows, "header": head,
            "caveats": [str(c) for c in (r.meta.get("caveats") or [])] + ([str(r.meta["scaling"])] if r.meta.get(
                "scaling") else [])}
