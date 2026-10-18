"""Parse physical quantities with SI prefixes: ``"100us"``, ``"10kHz"``, ``"1.5 V"``, ``"2e-6"``, ``"50%"``."""

from __future__ import annotations

import re

_PREFIX = {"": 1.0, "p": 1e-12, "n": 1e-9, "u": 1e-6, "µ": 1e-6, "m": 1e-3, "k": 1e3, "K": 1e3, "M": 1e6,
           "G": 1e9}
_UNITS = ("s", "Hz", "hz", "V", "v", "S/s", "Sa/s", "A", "ohm", "")
_RX = re.compile(r"^\s*([-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?)\s*([pnuµmkKMG]?)\s*(S/s|Sa/s|Hz|hz|s|V|v|ohm|A)?\s*$")


def parse_quantity(value: float | int | str, unit: str | None = None) -> float:
    """Return the value in base units. ``unit`` (e.g. ``"s"``) is only used to reject mismatched units."""
    if isinstance(value, (int, float)):
        return float(value)
    s = str(value).strip()
    if s.endswith("%"):
        return float(s[:-1]) / 100.0
    m = _RX.match(s)
    if not m:
        # "m" alone could be milli or metre; we only support SI-prefixed known units
        raise ValueError(f"cannot parse quantity {value!r}; use e.g. '100us', '10kHz', '0.5V' or a plain number")
    num, prefix, u = m.groups()
    if u and unit and u.lower().replace("sa/s", "s/s") != unit.lower().replace("sa/s", "s/s"):
        raise ValueError(f"{value!r}: expected a value in {unit}, got {u}")
    if prefix == "m" and not u and unit is None:
        pass
    return float(num) * _PREFIX[prefix]


def fmt_si(x: float, unit: str = "", digits: int = 4) -> str:
    if x == 0:
        return f"0 {unit}".strip()
    a = abs(x)
    for p, f in (("G", 1e9), ("M", 1e6), ("k", 1e3), ("", 1.0), ("m", 1e-3), ("u", 1e-6), ("n", 1e-9),
                 ("p", 1e-12)):
        if a >= f * 0.9999:
            return f"{x / f:.{digits}g} {p}{unit}".strip()
    return f"{x:.{digits}g} {unit}".strip()
