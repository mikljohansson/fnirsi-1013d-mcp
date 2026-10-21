"""Serial-bus decoders on captured waveforms: UART, SPI and I2C (docs/mcp-tools.md section 4.3b, tool
``scope_decode``).

Each decoder takes the time axis and the volts of the lines, turns every line into logic levels with a mid-level
threshold and 5 % hysteresis (``to_logic``), finds the edges with sub-sample interpolation and decodes from there.
The loops run per frame / per event, never per sample, so a 16k-sample record decodes in milliseconds.

Limits that every result states: a bit (or clock half-period) needs at least two samples to be seen at all and
about four for timing to be reliable (``MIN_SAMPLES_PER_BIT``); edges are placed to within a sample; levels are
8-bit codes, so a logic threshold inside the noise band gives spurious edges (the hysteresis absorbs about 10 % of
the swing).
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import Any

import numpy as np

#: samples per bit (UART) or per clock period (SPI, I2C) below which timing is unreliable
MIN_SAMPLES_PER_BIT = 4.0
#: below this the decoder refuses (a bit can fall between two samples)
HARD_MIN_SAMPLES_PER_BIT = 2.0
#: I2C: START / STOP are SDA edges inside the SCL-high half, which must span two samples on both sides of the edge
I2C_MIN_SAMPLES_PER_PERIOD = 8.0
I2C_HARD_MIN_SAMPLES_PER_PERIOD = 6.0
#: standard UART rates the auto-baud snaps to (within 4 %)
STANDARD_BAUDS = (300, 600, 1200, 2400, 4800, 9600, 14400, 19200, 28800, 31250, 38400, 57600, 76800, 115200, 230400,
                  250000, 460800, 500000, 921600, 1000000, 1500000, 2000000, 3000000, 4000000)


class DecodeError(ValueError):
    pass


@dataclass
class Logic:
    """One line as logic levels: ``level[i]`` (True = high) per sample, the threshold used, the swing between the
    2nd and 98th percentile, and every transition as a fractional sample index (mid-level crossing) with its
    direction."""

    level: np.ndarray
    threshold: float
    low: float
    high: float
    edges: np.ndarray = field(default_factory=lambda: np.empty(0))
    rising: np.ndarray = field(default_factory=lambda: np.empty(0, dtype=bool))

    @property
    def swing(self) -> float:
        return self.high - self.low

    def rising_edges(self) -> np.ndarray:
        return self.edges[self.rising]

    def falling_edges(self) -> np.ndarray:
        return self.edges[~self.rising]


def to_logic(v: np.ndarray, threshold: float | None = None, hysteresis: float = 0.05) -> Logic:
    """Logic levels of ``v`` (volts). ``threshold`` None: the middle between the 2nd and 98th percentile.
    ``hysteresis``: fraction of the swing on each side of the threshold the signal must pass before the level flips
    (noise inside that band makes no edges). Edges are the mid-threshold crossings, interpolated between samples."""
    v = np.asarray(v, dtype=np.float64)
    n = v.size
    if n < 4:
        raise DecodeError("fewer than 4 samples")
    # levels: the record's extremes, so a short pulse in a long idle record still counts (percentiles would miss a
    # 1 % duty line, a median filter a one-sample pulse); the noise must be well inside the swing
    lo, hi = float(v.min()), float(v.max())
    noise = float(np.median(np.abs(np.diff(v)))) * 1.4826 / math.sqrt(2)
    if hi - lo <= max(8 * noise, 1e-12):
        raise DecodeError(f"the line has no logic swing ({hi - lo:.3g} V between its extremes, noise about "
                          f"{noise:.3g} V rms): wrong channel, probe not connected, or the range is too coarse for the "
                          "logic levels")
    thr = 0.5 * (lo + hi) if threshold is None else float(threshold)
    if not lo <= thr <= hi:
        raise DecodeError(f"threshold {thr:.4g} V is outside the signal's levels {lo:.4g} .. {hi:.4g} V")
    h = hysteresis * (hi - lo)
    above = v > thr + h
    below = v < thr - h
    definite = above | below
    idx = np.where(definite, np.arange(n), -1)
    np.maximum.accumulate(idx, out=idx)
    level = np.where(idx >= 0, above[np.maximum(idx, 0)], v[0] > thr)
    change = np.flatnonzero(level[1:] != level[:-1]) + 1  # first sample index of the new level
    if change.size == 0:
        return Logic(level, thr, lo, hi)
    rising = level[change]
    # place the edge at the mid-threshold crossing just before the hysteresis flip
    up = np.flatnonzero((v[:-1] < thr) & (v[1:] >= thr))  # crossing between up and up + 1
    dn = np.flatnonzero((v[:-1] >= thr) & (v[1:] < thr))
    edges = np.empty(change.size, dtype=np.float64)
    for sel, cross in ((rising, up), (~rising, dn)):
        ci = change[sel]
        if cross.size == 0:
            edges[sel] = ci - 0.5
            continue
        k = np.searchsorted(cross, ci, side="right") - 1
        k = np.clip(k, 0, cross.size - 1)
        c = cross[k]
        # the crossing must lie within the previous level's run; otherwise fall back to the flip point
        bad = c > ci - 1
        a, b = v[c], v[c + 1]
        with np.errstate(divide="ignore", invalid="ignore"):
            frac = np.where(b != a, (thr - a) / (b - a), 0.5)
        edges[sel] = np.where(bad, ci - 0.5, c + np.clip(frac, 0.0, 1.0))
    return Logic(level, thr, lo, hi, edges, rising)


def samples_caveat(what: str, spb: float) -> str | None:
    if spb < MIN_SAMPLES_PER_BIT:
        return (f"only {spb:.1f} samples per {what}: at least {MIN_SAMPLES_PER_BIT:g} are needed for reliable "
                "timing; raise the sample rate")
    return None


def _char(b: int) -> str:
    return chr(b) if 32 <= b < 127 else "."


def _fmt_t(t: float) -> str:
    a = abs(t)
    if a >= 1.0:
        return f"{t:+.4f} s"
    if a >= 1e-3:
        return f"{t * 1e3:+.4f} ms"
    return f"{t * 1e6:+.3f} us"


# ---------------------------------------------------------------------------------------------------------
# UART


def auto_baud(lg: Logic, dt: float) -> tuple[float, float, bool]:
    """(baud, shortest pulse in seconds, snapped) from the shortest run between edges, snapped to a standard rate
    within 4 %."""
    if lg.edges.size < 2:
        raise DecodeError("auto-baud needs at least two edges on the line")
    widths = np.diff(lg.edges) * dt
    widths = widths[widths > 0.75 * dt]  # a one-sample glitch is not a bit
    if widths.size == 0:
        raise DecodeError("auto-baud: no pulse longer than one sample")
    shortest = float(widths.min())
    raw = 1.0 / shortest
    tol = max(0.04, 1.5 * dt / shortest)  # edges are placed to about a sample
    for b in STANDARD_BAUDS:
        if abs(raw - b) / b <= tol:
            return float(b), shortest, True
    return raw, shortest, False


def decode_uart(t: np.ndarray, v: np.ndarray, *, baud: float | None = None, data_bits: int = 8,
                parity: str = "none", stop_bits: float = 1, idle: str = "high",
                threshold: float | None = None) -> dict[str, Any]:
    """UART frames on one line. Returns ``frames`` (t of the start edge relative to the time axis, value, hex, char,
    errors), ``applied`` (baud, samples per bit, threshold, levels), ``preview`` lines and ``caveats``."""
    t = np.asarray(t, np.float64)
    v = np.asarray(v, np.float64)
    if t.size != v.size or t.size < 4:
        raise DecodeError("time axis and samples differ in length")
    if not 5 <= data_bits <= 9:
        raise DecodeError("data_bits must be 5..9")
    if parity not in ("none", "even", "odd"):
        raise DecodeError("parity must be 'none', 'even' or 'odd'")
    if idle not in ("high", "low"):
        raise DecodeError("idle must be 'high' or 'low'")
    dt = float(t[1] - t[0])
    lg = to_logic(v, threshold)
    cav: list[str] = []
    snapped = None
    if baud is None:
        baud, shortest, snapped = auto_baud(lg, dt)
        cav.append(f"auto-baud from the shortest pulse ({shortest * 1e6:.3g} us): {baud:.6g} baud"
                   + (" (snapped to a standard rate)" if snapped else " (no standard rate within 4 %: using the raw "
                                                                       "value; give baud= if you know it)"))
    spb = 1.0 / (baud * dt)
    if spb < HARD_MIN_SAMPLES_PER_BIT:
        raise DecodeError(f"{spb:.2f} samples per bit at {baud:g} baud: a bit can fall between two samples; raise "
                          f"the sample rate to at least {MIN_SAMPLES_PER_BIT * baud:.4g} Sa/s")
    c = samples_caveat("bit", spb)
    if c:
        cav.append(c)
    idle_high = idle == "high"
    starts = lg.edges[lg.rising != idle_high]  # a start bit leaves the idle level
    has_parity = parity != "none"
    nbits = 1 + data_bits + (1 if has_parity else 0)
    total_bits = nbits + stop_bits
    n = v.size
    frames: list[dict[str, Any]] = []
    n_err = 0
    pos = -math.inf
    incomplete = False
    for s in starts:
        if s < pos:
            continue
        centres = s + (np.arange(nbits) + 0.5) * spb
        stop_at = s + (nbits + np.arange(max(1, int(math.ceil(stop_bits)))) + 0.5) * spb
        stop_at = stop_at[stop_at < s + total_bits * spb]
        idx = np.rint(np.concatenate([centres, stop_at])).astype(np.int64)
        if idx[-1] >= n:
            incomplete = True
            break
        bits = lg.level[idx]
        ones = bits if idle_high else ~bits  # logic 1 = the idle (mark) level
        if ones[0]:
            continue  # the line was back at idle in the middle of the start bit: a glitch, not a frame
        data = ones[1:1 + data_bits]
        value = int(np.sum(data.astype(np.int64) << np.arange(data_bits)))
        errors = []
        if has_parity:
            even = (int(data.sum()) + int(ones[1 + data_bits])) % 2 == 0
            if (parity == "even") != even:
                errors.append("parity")
        if not np.all(ones[nbits:]):
            errors.append("framing")
        n_err += bool(errors)
        frames.append({"t": float(t[0] + s * dt), "value": value, "hex": f"0x{value:02X}", "char": _char(value),
                       "errors": errors})
        pos = s + total_bits * spb - 0.3 * spb
    if incomplete:
        cav.append("the record ends inside a frame: the last frame was dropped")
    if not frames and starts.size:
        cav.append("edges found but no frame decoded: check idle level, baud and data bits")
    if lg.swing < 0.3:
        cav.append(f"the line swings only {lg.swing:.3g} V between its levels: check the range (8-bit codes, "
                   "about 2 codes of noise)")
    preview = [f"{_fmt_t(f['t'])}  {f['hex']} {f['char']!r}" + (f"  {','.join(f['errors'])}" if f["errors"] else "")
               for f in frames[:64]]
    return {"protocol": "uart", "frames": frames, "n_frames": len(frames), "n_errors": n_err,
            "applied": {"baud": float(baud), "auto_baud": snapped is not None, "samples_per_bit": spb,
                        "data_bits": data_bits, "parity": parity, "stop_bits": stop_bits, "idle": idle,
                        "threshold_volts": lg.threshold, "levels_volts": [lg.low, lg.high]},
            "preview": preview, "caveats": cav}


# ---------------------------------------------------------------------------------------------------------
# SPI


def decode_spi(t: np.ndarray, clk: np.ndarray, data: np.ndarray, *, word_bits: int = 8, cpol: int = 0,
               cpha: int = 0, msb_first: bool = True, gap: float | None = None,
               threshold: float | None = None) -> dict[str, Any]:
    """SPI words from a clock line and one data line (MOSI or MISO; no chip select with two channels). Words are
    ``word_bits`` consecutive sampling edges; a pause of the clock longer than ``gap`` (default 2.5 clock periods)
    starts a new transfer, so a word cut by a gap or the record end is flagged ``incomplete``."""
    t = np.asarray(t, np.float64)
    if t.size < 4:
        raise DecodeError("fewer than 4 samples")
    if not 1 <= word_bits <= 32:
        raise DecodeError("word_bits must be 1..32")
    dt = float(t[1] - t[0])
    lc = to_logic(clk, threshold)
    ld = to_logic(data, threshold)
    cav: list[str] = []
    if lc.edges.size < 2:
        raise DecodeError("no clock edges on the clock channel: wrong channel, or the range hides the swing")
    leading_rising = cpol == 0  # idle low: the leading (first) edge of a cycle rises
    sample_rising = leading_rising if cpha == 0 else not leading_rising
    e = lc.edges[lc.rising == sample_rising]
    if e.size == 0:
        raise DecodeError("no sampling edges on the clock")
    half = float(np.median(np.diff(lc.edges))) * dt if lc.edges.size > 2 else float(np.diff(lc.edges)[0]) * dt
    period = 2 * half
    spp = period / dt
    c = samples_caveat("clock period", spp)
    if c:
        cav.append(c)
    if spp < HARD_MIN_SAMPLES_PER_BIT:
        raise DecodeError(f"{spp:.2f} samples per clock period ({1 / period:.4g} Hz): raise the sample rate to at "
                          f"least {MIN_SAMPLES_PER_BIT / period:.4g} Sa/s")
    # the data is stable half a period around the sampling edge: read it one quarter period after the edge
    idx = np.clip(np.rint(e + 0.25 * spp).astype(np.int64), 0, ld.level.size - 1)
    bits = ld.level[idx].astype(np.int64)
    gap_s = gap if gap is not None else 2.5 * period
    new = np.concatenate([[True], np.diff(e) * dt > gap_s])
    transfer = np.cumsum(new) - 1
    words: list[dict[str, Any]] = []
    n_inc = 0
    for tr in range(int(transfer[-1]) + 1):
        sel = np.flatnonzero(transfer == tr)
        for k in range(0, sel.size, word_bits):
            part = sel[k:k + word_bits]
            b = bits[part]
            nb = b.size
            if msb_first:
                value = int(np.sum(b << np.arange(nb - 1, -1, -1)))
            else:
                value = int(np.sum(b << np.arange(nb)))
            inc = nb < word_bits
            n_inc += inc
            w = {"t": float(t[0] + e[part[0]] * dt), "transfer": tr, "value": value,
                 "hex": f"0x{value:0{max(2, (nb + 3) // 4)}X}", "bits": nb,
                 "errors": ["incomplete"] if inc else []}
            if word_bits == 8:
                w["char"] = _char(value)
            words.append(w)
    if n_inc:
        cav.append(f"{n_inc} word(s) have fewer than {word_bits} clocks (a gap or the record end cut them)")
    cav.append("no chip select on two channels: words are framed by the clock count from the first clock after a "
               f"pause longer than {gap_s * 1e6:.3g} us; a transfer that starts before the record does is misaligned")
    preview = [f"{_fmt_t(w['t'])}  {w['hex']}" + (f" {w['char']!r}" if "char" in w else "")
               + (f"  {','.join(w['errors'])}" if w["errors"] else "") for w in words[:64]]
    return {"protocol": "spi", "frames": words, "n_frames": len(words), "n_errors": n_inc,
            "applied": {"clock_hz": 1.0 / period, "samples_per_clock": spp, "word_bits": word_bits, "cpol": cpol,
                        "cpha": cpha, "msb_first": msb_first, "gap_s": gap_s, "sampling_edge":
                        "rising" if sample_rising else "falling", "threshold_volts": [lc.threshold, ld.threshold],
                        "levels_volts": {"clock": [lc.low, lc.high], "data": [ld.low, ld.high]}},
            "preview": preview, "caveats": cav}


# ---------------------------------------------------------------------------------------------------------
# I2C


def decode_i2c(t: np.ndarray, scl: np.ndarray, sda: np.ndarray, *, threshold: float | None = None) -> dict[str, Any]:
    """I2C transactions from SCL and SDA: START / repeated START, the address byte (7-bit address and R/W, 10-bit
    addresses flagged), the ACK of every byte, the data bytes, STOP. SDA is sampled right after each SCL rising
    edge; an SDA edge while SCL is high is a START (falling) or STOP (rising)."""
    t = np.asarray(t, np.float64)
    if t.size < 4:
        raise DecodeError("fewer than 4 samples")
    dt = float(t[1] - t[0])
    ls = to_logic(scl, threshold)
    ld = to_logic(sda, threshold)
    cav: list[str] = []
    rises = ls.rising_edges()
    if rises.size < 2:
        raise DecodeError("fewer than two SCL rising edges: wrong channel, or the range hides the swing")
    period = float(np.median(np.diff(rises))) * dt
    spp = period / dt
    if spp < I2C_MIN_SAMPLES_PER_PERIOD:
        cav.append(f"only {spp:.1f} samples per SCL period: at least {I2C_MIN_SAMPLES_PER_PERIOD:g} are needed to "
                   "tell START / STOP (an SDA edge inside the SCL-high half) from data bits; raise the sample rate")
    if spp < I2C_HARD_MIN_SAMPLES_PER_PERIOD:
        raise DecodeError(f"{spp:.2f} samples per SCL period ({1 / period:.4g} Hz): raise the sample rate to at least "
                          f"{I2C_MIN_SAMPLES_PER_PERIOD / period:.4g} Sa/s")
    n = ls.level.size
    # bits: SDA just after the SCL rising edge
    bit_idx = np.clip(np.ceil(rises).astype(np.int64), 0, n - 1)
    bit_val = ld.level[bit_idx]
    # START / STOP: SDA edges while SCL is high on both sides
    se = ld.edges
    before = np.clip(np.floor(se).astype(np.int64), 0, n - 1)
    after = np.clip(np.ceil(se).astype(np.int64), 0, n - 1)
    while_high = ls.level[before] & ls.level[after]
    cond = se[while_high]
    cond_rising = ld.rising[while_high]
    events = sorted([(float(p), 0, int(b)) for p, b in zip(rises, bit_val)]
                    + [(float(p), 1, int(r)) for p, r in zip(cond, cond_rising)])  # (position, kind, value)
    falls = ls.falling_edges()
    frames: list[dict[str, Any]] = []
    cur: dict[str, Any] | None = None
    bits: list[int] = []
    last_bit_pos = -math.inf
    n_err = 0

    def close(reason: str | None) -> None:
        nonlocal cur, bits
        if cur is None:
            return
        if bits:
            cur["errors"].append(f"{len(bits)} stray bit(s) before {reason or 'the record end'}")
        if reason is None:
            cur["errors"].append("no STOP (record ends inside the transaction)")
        cur["stop"] = reason == "STOP"
        cur["repeated_start"] = reason == "START"
        frames.append(cur)
        cur = None
        bits = []

    for pos, kind, val in events:
        if kind == 1:
            # the SCL rising edge of a START / STOP condition is not a data bit: drop a bit sampled in this same
            # SCL-high phase (no SCL falling edge between it and the condition)
            k = int(np.searchsorted(falls, pos)) - 1
            last_fall = float(falls[k]) if k >= 0 else -math.inf
            if bits and last_bit_pos > last_fall:
                bits.pop()
            if val == 0:  # SDA falls while SCL high: START
                close("START")
                cur = {"t": float(t[0] + pos * dt), "address": None, "address_7bit": None, "rw": None,
                       "ack": None, "data": [], "errors": [], "stop": False, "repeated_start": False}
            else:
                close("STOP")
            continue
        if cur is None:
            continue  # bits before the first START: an earlier transaction cut by the record start
        bits.append(val)
        last_bit_pos = pos
        if len(bits) == 9:
            byte = int(sum(b << (7 - i) for i, b in enumerate(bits[:8])))
            ack = bits[8] == 0
            if cur["address"] is None:
                cur["address"] = f"0x{byte >> 1:02X}"
                cur["address_7bit"] = byte >> 1
                cur["rw"] = "read" if byte & 1 else "write"
                cur["ack"] = ack
                if (byte >> 3) == 0b11110:
                    cur["errors"].append("10-bit address prefix: the second address byte is listed as data")
                if not ack:
                    cur["errors"].append("NACK on the address (no slave answered)")
            else:
                cur["data"].append({"hex": f"0x{byte:02X}", "value": byte, "ack": ack})
                if not ack and cur["rw"] == "write":
                    cur["errors"].append(f"NACK on data byte {len(cur['data'])}")
            bits = []
    close(None)
    for f in frames:
        f["n_data"] = len(f["data"])
        n_err += bool(f["errors"])
    if events and frames == [] and bit_val.size:
        cav.append("SCL clocks but no START seen: the record starts inside a transaction, or SDA is on the wrong "
                   "channel")
    leading = sum(1 for p, k, _ in events if k == 0 and (not frames or p * dt + t[0] < frames[0]["t"]))
    if frames and leading:
        cav.append(f"{leading} clock(s) before the first START belong to a transaction cut by the record start")
    preview = []
    for f in frames[:32]:
        parts = [f"S {f['address']} {'R' if f['rw'] == 'read' else 'W'} {'ACK' if f['ack'] else 'NACK'}"]
        parts += [f"{d['hex']} {'ACK' if d['ack'] else 'NACK'}" for d in f["data"][:16]]
        if len(f["data"]) > 16:
            parts.append(f"... {len(f['data']) - 16} more")
        tail = " P" if f["stop"] else (" Sr" if f["repeated_start"] else "")
        preview.append(f"{_fmt_t(f['t'])}  " + " | ".join(parts) + tail
                       + (f"  {'; '.join(f['errors'])}" if f["errors"] else ""))
    return {"protocol": "i2c", "frames": frames, "n_frames": len(frames), "n_errors": n_err,
            "applied": {"scl_hz": 1.0 / period, "samples_per_scl_period": spp,
                        "threshold_volts": [ls.threshold, ld.threshold],
                        "levels_volts": {"scl": [ls.low, ls.high], "sda": [ld.low, ld.high]}},
            "preview": preview, "caveats": cav}


STANDING = ["8-bit samples: a logic threshold inside the noise band (about 2 codes) makes spurious edges; the decoder "
            "uses 5 % of the swing as hysteresis on each side",
            "edges are placed to within one sample; bit timing needs at least 4 samples per bit (or clock period) "
            "to be reliable, 2 to be decodable at all"]
