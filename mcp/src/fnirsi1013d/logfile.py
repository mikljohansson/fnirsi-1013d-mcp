"""The streaming logger on the host (protocol 1.27, FPGA 0x153D, docs/plans/fpga-0x153c.md section 6).

A log is a directory under the data dir (``<data_dir>/<date>/<time>_log/``):

- ``CH1.u8`` / ``CH1.u16`` (little-endian) / ``CH1.min.u8`` + ``CH1.max.u8``: the raw sample stream per channel,
  appended as LOG_READ chunks arrive (np.fromfile / np.memmap read it; a long log never has to fit in memory). Format
  0 (raw 8-bit) holds both ADC cores in time order (core B, core A, B, A, ...); the other formats one sample per
  ring word.
- ``log.json``: the manifest, rewritten about once a second and at the end: rate, format, channels, the volts model
  per channel and lane (``volts = a x code + b``: linear in every format, the scope's calibration evaluated at the
  start), the gaps (``sample`` = the index in the file where samples are missing, ``lost`` = how many per channel,
  ``cause`` = 'lap' (the scope's MCU fell behind the FPGA ring) or 'overflow' (the host fell behind the scope's DDR
  ring)), the state and the caveats.
- ``<time>_log.npz`` beside the directory: the summary ``scope_report`` renders (kind 'log'): per channel the min,
  max and mean volts of up to 4000 bins over the whole log, the bin times and the gap times.

``LogPlan`` chooses the format, divider and filter from a rate; ``LogRun`` pumps LOG_READ in a thread (one request at
a time under the session's lock, so other tools interleave) and writes the files; ``load_log`` reads a log back as
time and volts arrays.
"""

from __future__ import annotations

import datetime as _dt
import json
import math
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

import numpy as np

from . import protocol as P
from .scope import CHANNEL_NAMES, Scope, ScopeError, channel_index
from .transport import DeviceError, TransportError

#: summary bins over the whole log (the report's plot resolution)
SUMMARY_BINS = 4000
#: manifest rewrite interval while logging
MANIFEST_EVERY_S = 1.0
TIER_TEXT = ("the logger's documented tiers (contract 6.2): 500 kS/s for one 8-bit channel, 250 kS/s for one 16-bit "
             "or peak channel or two 8-bit channels, 125 kS/s for two 16-bit channels; a higher tier only after the "
             "bench measures it")
FILTERED8_NOTE = ("8-bit filtered: each sample is the high byte of the CIC / FIR filter's 16-bit word (the filter's "
                  "anti-alias at 8-bit cost: about 0.4 % of full scale per code)")
RAW8_NOTE = ("raw 8-bit: both ADC cores at 200 MSa/s / (D + 1) without the decimation filter: a signal above half the "
             "rate aliases")
FROZEN_NOTE = ("the scope's screen is frozen ('Logging to the host') and every acquisition command answers 'logging: "
               "LOG_STOP first' until scope_log_stop or the duration's end; set up the generator before the log")


@dataclass
class LogPlan:
    """What LOG_START is sent with: ``fmt`` (P.LOG_FMT_*), the ``divider``, the SET_DECIM ``filter`` to set first (None
    = filter off), the resulting per-channel ``rate`` and the caveats of the choice."""

    channels: int
    fmt: int
    divider: int
    filter: str | None
    rate: float
    caveats: list[str] = field(default_factory=list)

    @property
    def bytes_per_s(self) -> float:
        return P.log_bytes_per_s(self.channels, self.fmt, self.divider)

    @property
    def names(self) -> list[str]:
        return [CHANNEL_NAMES[c] for c in (0, 1) if self.channels >> c & 1]

    def to_dict(self) -> dict[str, Any]:
        return {"channels": self.names, "format": P.LOG_FORMAT_NAMES[self.fmt], "divider": self.divider,
                "filter": self.filter or "off", "rate": self.rate, "bytes_per_s": self.bytes_per_s}


def channel_mask(channels: list[str] | str) -> int:
    names = [channels] if isinstance(channels, str) else list(channels)
    mask = 0
    for c in names:
        mask |= 1 << channel_index(c)
    if not mask:
        raise ValueError("log at least one channel")
    return mask


def plan_log(rate_hz: float, channels: list[str] | str = "CH1", bits: int = 8, filter: str = "auto",
             fir: bool = True) -> LogPlan:
    """The format and divider for a per-channel sample rate (contract 6.4): 'auto' = the decimation filter (CIC3, with
    the FIR where N allows it) at 8 bits (the filtered word's high byte) or 16; 'off' = raw 8-bit legacy samples (both
    cores, no anti-alias); 'peak' = peak-detect min / max pairs. The rate snaps to the divider grid (200 MHz / N with
    an even N filtered, 200 MHz / (D + 1) raw) and must be within the tiers (TIER_TEXT)."""
    mask = channel_mask(channels)
    if bits not in (8, 16):
        raise ValueError("bits: 8 or 16")
    if not rate_hz > 0:
        raise ValueError("rate must be > 0")
    f = str(filter).lower()
    cav: list[str] = []
    if f == "off":
        if bits != 8:
            raise ValueError("filter='off' logs raw 8-bit samples; bits=16 needs the filter")
        fmt, name = P.LOG_FMT_RAW8, None
        divider = max(0, int(round(P.BASE_CLOCK_HZ / rate_hz)) - 1)
        cav.append(RAW8_NOTE)
    else:
        if f == "peak":
            fmt, name = P.LOG_FMT_PEAK, "peak"
        elif f in ("auto", "cic3", "cic3+fir", "cic2", "cic2+fir", "boxcar", "boxcar+fir"):
            fmt = P.LOG_FMT_HIGH8 if bits == 8 else P.LOG_FMT_WIDE16
            name = ("cic3+fir" if fir else "cic3") if f == "auto" else f
        else:
            raise ValueError(f"filter {filter!r}: 'auto', 'off', 'peak' or a filter name (cic3+fir, cic2, boxcar, ...)")
        n = max(2, 2 * int(round(P.BASE_CLOCK_HZ / 2 / rate_hz)))
        if name and name.endswith("+fir") and n >= P.DECIM_FIR_MIN_N and n % 4:
            n4 = 4 * int(round(n / 4))  # the FIR needs N % 4 == 0: the nearest such N
            n = n4 if abs(n4 - n) <= 2 else n
        divider = n - 1
        if fmt == P.LOG_FMT_HIGH8:
            cav.append(FILTERED8_NOTE)
    plan = LogPlan(mask, fmt, divider, name, P.log_sample_rate(fmt, divider), cav)
    why = P.log_check(mask, fmt, divider)
    if why is not None:
        raise ValueError(f"{why[1]}; {TIER_TEXT}")
    if abs(plan.rate / rate_hz - 1) > 1e-3:
        cav.append(f"rate {rate_hz:g} S/s snapped to {plan.rate:.6g} S/s (the divider grid)")
    return plan


def decode_chunk(data: bytes, channels: int, fmt: int) -> dict[str, list[np.ndarray]]:
    """LOG_READ bytes -> per channel name the streams (format 0: one stream of samples in time order; 1: uint8; 2:
    uint16; 3: [min, max])."""
    names = [CHANNEL_NAMES[c] for c in (0, 1) if channels >> c & 1]
    k = len(names)
    out: dict[str, list[np.ndarray]] = {}
    if fmt == P.LOG_FMT_WIDE16:
        a = np.frombuffer(data, "<u2").reshape(-1, k)
        for i, nm in enumerate(names):
            out[nm] = [a[:, i]]
        return out
    a = np.frombuffer(data, np.uint8)
    per = 2 if fmt in (P.LOG_FMT_RAW8, P.LOG_FMT_PEAK) else 1
    a = a.reshape(-1, k, per)
    for i, nm in enumerate(names):
        if fmt == P.LOG_FMT_PEAK:
            out[nm] = [a[:, i, 0], a[:, i, 1]]
        else:
            out[nm] = [a[:, i, :].reshape(-1)]
    return out


def volts_model(s: Scope, plan: LogPlan) -> dict[str, dict[str, Any]]:
    """Per channel the linear code -> volts map of the log's samples, from the scope's calibration now (contract 6.4
    'volts model'): format 0 per core (lane 0 = core B, the earlier sample of each word; lane 1 = core A) in the clock
    class of the divider; formats 1 / 2 / 3 the mean of both cores at full rate, the filter's scale included (format 1
    reads the high byte: x 256)."""
    out: dict[str, dict[str, Any]] = {}
    dec = None
    if plan.fmt != P.LOG_FMT_RAW8:
        st = s.get_decim()
        has = P.DECIM_HAS_FIR | (P.DECIM_HAS_CAPSTAGES if s.info.caps & P.Caps.CAPTURE_STAGES else 0)
        dec = P.decim_config(st.filter, (st.flags & 0x0F) << 2, plan.divider, 2000, has)  # SetDecimReq bits -> the word
    for c in (0, 1):
        if not plan.channels >> c & 1:
            continue
        cfg = s.get_channel(c, cached=False)
        sgn = -1.0 if cfg.invert else 1.0
        lanes = []
        if plan.fmt == P.LOG_FMT_RAW8:
            sc = s.scale(c, cfg)
            for core in ("B", "A"):
                v0, v1 = (float(x) for x in sc.volts_raw(np.array([0.0, 255.0]), core))
                lanes.append([sgn * (v1 - v0) / 255.0, sgn * v0])
        else:
            sc = s.scale(c, cfg)
            assert dec is not None
            per = {P.LOG_FMT_HIGH8: 256 * dec.code_per_count, P.LOG_FMT_WIDE16: dec.code_per_count,
                   P.LOG_FMT_PEAK: 1.0}[plan.fmt]
            v0, v1 = (float(x) for x in sc.volts_mean_raw(np.array([0.0, 100.0 * per])))
            lanes.append([sgn * (v1 - v0) / 100.0, sgn * v0])
        out[CHANNEL_NAMES[c]] = {"lanes": lanes, "range": cfg.range, "probe_index": cfg.probe_index,
                                 "invert": bool(cfg.invert), "source": sc.source}
    return out


class LogRun:
    """One log: LOG_START, the pump thread (LOG_READ chunks -> files), the end (duration, scope_log_stop, an error
    or a change at the scope), the summary. ``lock`` is held for every scope request (the MCP session's lock);
    ``on_activity`` runs after each (the session's idle clock)."""

    def __init__(self, scope: Scope, plan: LogPlan, directory: Path, *, duration_s: float | None = None,
                 lock: Any = None, on_activity: Callable[[], None] | None = None, restore: Callable[[], list[str]]
                 | None = None, args: dict[str, Any] | None = None):
        self.s = scope
        self.plan = plan
        self.dir = Path(directory)
        self.duration_s = duration_s
        self.lock = lock or threading.Lock()
        self.on_activity = on_activity
        self.restore = restore
        self.args = args or {}
        self.stop_requested = threading.Event()
        self.thread: threading.Thread | None = None
        self.started = _dt.datetime.now()
        self.t0 = time.monotonic()
        self.t_end: float | None = None
        self.samples = 0  # per channel, in the files
        self.bytes = 0
        self.chunks = 0
        self.gaps: list[dict[str, Any]] = []
        self.lost = 0
        self.state = "starting"
        self.error: str | None = None
        self.final: P.LogResp | None = None
        self.start_resp: P.LogResp | None = None
        self.settings_seq: int | None = None
        self.caveats = list(plan.caveats)
        self.summary_file: str | None = None
        self.volts: dict[str, dict[str, Any]] = {}
        self._files: dict[str, list[Any]] = {}
        self._last_manifest = 0.0
        self._bins: dict[str, list[np.ndarray]] = {}

    # files -------------------------------------------------------------------------------------------------

    def _open(self) -> None:
        self.dir.mkdir(parents=True, exist_ok=True)
        for nm in self.plan.names:
            if self.plan.fmt == P.LOG_FMT_PEAK:
                paths = [self.dir / f"{nm}.min.u8", self.dir / f"{nm}.max.u8"]
            else:
                paths = [self.dir / f"{nm}.{'u16' if self.plan.fmt == P.LOG_FMT_WIDE16 else 'u8'}"]
            self._files[nm] = [open(p, "wb") for p in paths]

    def _close(self) -> None:
        for fs in self._files.values():
            for f in fs:
                f.close()
        self._files = {}

    @property
    def files(self) -> dict[str, list[str]]:
        out = {}
        for nm in self.plan.names:
            if self.plan.fmt == P.LOG_FMT_PEAK:
                out[nm] = [str(self.dir / f"{nm}.min.u8"), str(self.dir / f"{nm}.max.u8")]
            else:
                out[nm] = [str(self.dir / f"{nm}.{'u16' if self.plan.fmt == P.LOG_FMT_WIDE16 else 'u8'}")]
        return out

    def manifest(self) -> dict[str, Any]:
        sps = P.log_samples_per_word(self.plan.fmt)
        return {"format": "fnirsi1013d-log", "version": 1, "started": self.started.isoformat(timespec="milliseconds"),
                "plan": self.plan.to_dict(), "sample_rate": self.plan.rate, "sample_period_s": 1.0 / self.plan.rate,
                "samples_per_word": sps, "word_period_s": sps / self.plan.rate,
                "dtype": "uint16" if self.plan.fmt == P.LOG_FMT_WIDE16 else "uint8",
                "lanes_note": "volts = a x code + b per lane; raw8: lane 0 = core B (even samples), lane 1 = core A",
                "volts": self.volts, "files": self.files, "samples": self.samples, "gaps": self.gaps,
                "lost_samples": self.lost, "state": self.state, "error": self.error,
                "duration_requested_s": self.duration_s, "elapsed_s": self.elapsed_s, "bytes": self.bytes,
                "device": self.final.to_dict() if self.final else None, "caveats": self.caveats, "args": self.args}

    def _write_manifest(self) -> None:
        tmp = self.dir / "log.json.tmp"
        tmp.write_text(json.dumps(self.manifest(), indent=1, default=str), encoding="utf-8")
        tmp.replace(self.dir / "log.json")
        self._last_manifest = time.monotonic()

    # scope requests ----------------------------------------------------------------------------------------

    def _locked(self, fn: Callable[[], Any]) -> Any:
        with self.lock:
            try:
                return fn()
            finally:
                if self.on_activity is not None:
                    self.on_activity()

    def start(self) -> P.LogResp:
        """LOG_START (the caller set the filter first) and the pump thread."""
        self.volts = self._locked(lambda: volts_model(self.s, self.plan))
        st = self._locked(self.s.status)
        self.settings_seq = st.settings_seq
        self._open()
        try:
            self.start_resp = self._locked(lambda: self.s.log_start(self.plan.channels, self.plan.fmt,
                                                                    self.plan.divider, self.duration_s))
        except Exception:
            self._close()
            raise
        self.t0 = time.monotonic()
        self.state = "running"
        self._write_manifest()
        self.thread = threading.Thread(target=self._pump, name="fnirsi-log", daemon=True)
        self.thread.start()
        return self.start_resp

    def _pump(self) -> None:
        try:
            self._drain(stop_on_request=True)
            if self.stop_requested.is_set() and self.state == "running":
                self._locked(self.s.log_stop)
                self._drain(stop_on_request=False)
        except (DeviceError, TransportError, ScopeError, OSError) as e:
            self.error = f"{type(e).__name__}: {e}"
        finally:
            self._finish()

    def _drain(self, stop_on_request: bool) -> None:
        wb = P.log_word_bytes(self.plan.channels, self.plan.fmt)
        sps = P.log_samples_per_word(self.plan.fmt)
        next_word = self.samples // sps + self.lost // sps
        while True:
            if stop_on_request and self.stop_requested.is_set():
                return
            h, data = self._locked(self.s.log_read)
            if h.nbytes:
                if h.nbytes % wb:
                    raise ScopeError(f"LOG_READ returned {h.nbytes} bytes, not whole words of {wb}")
                lost = (h.word - next_word) & 0xFFFFFFFF
                if lost or h.gap:
                    cause = "overflow" if h.flags & P.LOG_CHUNK_OVERFLOW else ("lap" if h.gap else "unflagged")
                    self.gaps.append({"sample": self.samples, "lost": lost * sps, "cause": cause,
                                      "t_s": (self.samples + self.lost) / self.plan.rate})
                    self.lost += lost * sps
                    if cause == "unflagged":
                        self.caveats.append("words went missing without a gap flag (a LOG_READ answer lost on the "
                                            "link?): the word counter shows where")
                nwords = h.nbytes // wb
                next_word += lost + nwords
                self._append(data)
                self.chunks += 1
                self.bytes += h.nbytes
            if time.monotonic() - self._last_manifest > MANIFEST_EVERY_S:
                self._write_manifest()
            if h.ended and not h.nbytes:
                return
            if not h.nbytes:
                time.sleep(0.005)

    def _append(self, data: bytes) -> None:
        streams = decode_chunk(data, self.plan.channels, self.plan.fmt)
        n = 0
        for nm, arrs in streams.items():
            for f, a in zip(self._files[nm], arrs):
                a.tofile(f)
            n = arrs[0].size
        self.samples += n

    def _finish(self) -> None:
        self._close()
        try:
            if self.final is None:
                self.final = self._locked(self.s.log_stop)  # idempotent: the LOG as it stands
            self.state = self.final.state_name
        except (DeviceError, TransportError) as e:
            self.error = self.error or f"{type(e).__name__}: {e}"
            self.state = "error"
        self.t_end = time.monotonic()
        if self.final is not None and self.final.state == P.LOG_ERROR:
            changed = ""
            try:
                st = self._locked(self.s.status)
                if self.settings_seq is not None and st.settings_seq != self.settings_seq:
                    changed = ": a setting was changed at the scope"
            except (DeviceError, TransportError):
                pass
            self.caveats.append(f"the log ended early on the scope{changed} (state error: the FPGA left stream "
                                "mode, a setting changed at the scope, or no LOG_READ for 10 s); the data up to there "
                                "are kept")
        if self.gaps:
            self.caveats.append(f"{len(self.gaps)} gap(s), {self.lost} samples per channel lost "
                                f"({sum(g['cause'] == 'overflow' for g in self.gaps)} because the host read too "
                                "slowly, the rest FPGA ring laps on the scope); the gaps are listed in log.json with "
                                "their sample index and marked in the report")
        if self.restore is not None:
            try:
                for note in self._locked(self.restore):
                    self.caveats.append(note)
            except (DeviceError, TransportError, ValueError) as e:
                self.caveats.append(f"restoring the settings after the log failed: {e}")
        try:
            self._write_manifest()
            self.summary_file = write_summary(self.dir, self.manifest())
        except (OSError, ValueError) as e:
            self.error = self.error or f"summary: {e}"

    # control -----------------------------------------------------------------------------------------------

    def request_stop(self, timeout_s: float = 30.0) -> None:
        self.stop_requested.set()
        if self.thread is not None:
            self.thread.join(timeout_s)

    @property
    def running(self) -> bool:
        return self.thread is not None and self.thread.is_alive()

    @property
    def elapsed_s(self) -> float:
        return (self.t_end or time.monotonic()) - self.t0

    def status(self) -> dict[str, Any]:
        el = self.elapsed_s
        return {"state": self.state if not self.running else "running", "directory": str(self.dir),
                "files": self.files, "plan": self.plan.to_dict(), "samples": self.samples,
                "seconds_logged": self.samples / self.plan.rate, "elapsed_s": el, "bytes": self.bytes,
                "usb_bytes_per_s": self.bytes / el if el > 0 else None, "gaps": len(self.gaps),
                "lost_samples": self.lost, "error": self.error, "summary": self.summary_file}


# ---------------------------------------------------------------------------------------------------------
# Reading a log back


def _range(d: Path, man: dict[str, Any], nm: str, s0: int, n: int) -> dict[str, np.ndarray]:
    """Samples s0 .. s0 + n - 1 of one channel: their times (the gaps included) and volts (lo / hi = the min / max of
    a peak log, else the volts)."""
    rate = float(man["sample_rate"])
    idx = np.arange(s0, s0 + n, dtype=np.int64)
    off = np.zeros(n)
    for g in man.get("gaps", []):
        off += np.where(idx >= int(g["sample"]), int(g["lost"]), 0)
    fmt = P.LOG_FORMAT_NAMES.index(man["plan"]["format"])
    vm = man["volts"][nm]["lanes"]
    dt = np.dtype(np.uint16 if man["dtype"] == "uint16" else np.uint8)
    raw = [np.fromfile(p, dtype=dt, count=n, offset=s0 * dt.itemsize).astype(np.float64) for p in man["files"][nm]]
    if fmt == P.LOG_FMT_RAW8:
        lane = idx % 2
        v = raw[0] * np.array([vm[0][0], vm[1][0]])[lane] + np.array([vm[0][1], vm[1][1]])[lane]
        lo = hi = v
    elif fmt == P.LOG_FMT_PEAK:
        lo = raw[0] * vm[0][0] + vm[0][1]
        hi = raw[1] * vm[0][0] + vm[0][1]
        v = (lo + hi) / 2
    else:
        v = raw[0] * vm[0][0] + vm[0][1]
        lo = hi = v
    return {"t": (idx + off) / rate, "v": v, "lo": lo, "hi": hi}


def load_log(directory: str | Path, channels: list[str] | None = None, start_s: float = 0.0,
             max_samples: int | None = None) -> dict[str, Any]:
    """A log as arrays: ``t`` (seconds since the start, the gaps included in the time axis), per channel
    ``<CH>_volts`` (and ``<CH>_min_volts`` / ``<CH>_max_volts`` for a peak log) from ``start_s`` on, at most
    ``max_samples`` per channel, and the manifest as ``meta``."""
    d = Path(directory)
    man = json.loads((d / "log.json").read_text(encoding="utf-8"))
    total = int(man["samples"])
    i0 = 0
    if start_s > 0:
        lost_before = sum(int(g["lost"]) for g in man.get("gaps", []) if float(g.get("t_s", 0.0)) < start_s)
        i0 = min(total, max(0, int(math.ceil(start_s * float(man["sample_rate"]))) - lost_before))
    n = total - i0 if max_samples is None else min(total - i0, max_samples)
    out: dict[str, Any] = {"meta": man}
    for nm in channels or list(man["files"]):
        r = _range(d, man, nm, i0, n)
        out["t"] = r["t"]
        out[f"{nm}_volts"] = r["v"]
        if man["plan"]["format"] == "peak":
            out[f"{nm}_min_volts"], out[f"{nm}_max_volts"] = r["lo"], r["hi"]
    return out


def write_summary(directory: Path, man: dict[str, Any], bins: int = SUMMARY_BINS, block: int = 1 << 20) -> str:
    """The report's summary ``.npz`` beside the log directory (kind 'log'): per channel the min / max / mean volts of
    up to ``bins`` bins, read in blocks of ``block`` samples (a long log never in memory at once), the bin centre
    times and the gap times."""
    d = Path(directory)
    total = int(man["samples"])
    arrays: dict[str, np.ndarray] = {}
    nb = min(bins, total)
    edges = np.linspace(0, total, nb + 1).astype(np.int64) if nb else np.zeros(1, np.int64)
    centre = (edges[:-1] + edges[1:]) // 2
    tt = np.zeros(nb)
    for nm in man["files"]:
        mn, mx = np.full(nb, np.inf), np.full(nb, -np.inf)
        sm, cnt = np.zeros(nb), np.zeros(nb)
        for s0 in range(0, total, block):
            r = _range(d, man, nm, s0, min(block, total - s0))
            k = np.searchsorted(edges, np.arange(s0, s0 + r["v"].size), side="right") - 1
            np.minimum.at(mn, k, r["lo"])
            np.maximum.at(mx, k, r["hi"])
            np.add.at(sm, k, r["v"])
            np.add.at(cnt, k, 1)
            inb = (centre >= s0) & (centre < s0 + r["v"].size)
            tt[inb] = r["t"][centre[inb] - s0]
        arrays[f"{nm}_min_volts"], arrays[f"{nm}_max_volts"] = mn, mx
        arrays[f"{nm}_mean_volts"] = sm / np.maximum(cnt, 1)
    arrays["t"] = tt
    gaps = man.get("gaps", [])
    arrays["gap_t"] = np.array([float(g.get("t_s", 0.0)) for g in gaps])
    arrays["gap_lost"] = np.array([int(g["lost"]) for g in gaps], np.int64)
    meta = {"kind": "log", "directory": str(d), "sample_rate": float(man["sample_rate"]), "samples": total,
            "format": man["plan"]["format"], "channels": list(man["files"]), "state": man.get("state"),
            "gaps": len(gaps), "lost_samples": man.get("lost_samples", 0), "elapsed_s": man.get("elapsed_s"),
            "started": man.get("started"), "caveats": man.get("caveats", []), "args": man.get("args", {}),
            "bins": nb, "samples_per_bin": int(math.ceil(total / nb)) if nb else 0}
    path = d.parent / f"{d.name}.npz"
    np.savez_compressed(path, meta_json=np.array(json.dumps(meta, default=str)), **arrays)
    return str(path)
