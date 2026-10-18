"""High-level client for the FNIRSI 1013D: numpy arrays and physical units.

    from fnirsi1013d.scope import Scope
    with Scope.connect("tcp://localhost:5555") as s:
        s.take_remote()
        s.set_timebase(divider=9)                       # 20 MS/s
        s.set_trigger(source=1, edge=1, level=160, mode=1, pretrigger_samples=800, total_samples=16000)
        cap = s.capture(timeout_s=1.0)
        v = cap.channels["CH1"].volts                    # numpy, volts at the probe tip
        t = cap.t                                        # seconds relative to the trigger

Scaling [U, bench test T8, docs/protocol.md section 8.5]: the firmware reports ``uv_per_lsb_q8`` and
``zero_code_q8`` per channel, derived from ``input_calibration``, the 1x ``mul_factor`` table and the DC
shift calibration. The host computes ``volts = (code_compensated - zero_code) x volts_per_lsb x probe``.
Until T8 has been run on the owner's unit, absolute volts are an estimate (relative shapes, time constants and
frequencies are not affected).
"""

from __future__ import annotations

import threading
import time
from dataclasses import dataclass
from typing import Any, Callable, Iterable

import numpy as np

from . import protocol as P
from .transport import DeviceError, Transport, TransportError, connect as _connect

CHANNEL_NAMES = ("CH1", "CH2")

#: Static caveats (docs/mcp-tools.md section 3).
SCALING_CAVEAT = ("Absolute volts use the firmware's uv_per_lsb/zero_code formula (protocol 8.5), not yet "
                  "verified against a reference voltage (bench test T8).")


#: Provisional cap for serial links on Windows (bench 2026-10-18, firmware 0.1.0: 16 KiB READ_RAW chunks timed
#: out intermittently, 8 KiB worked throughout). To be re-measured on firmware 0.1.1 and dropped if 64 KiB is clean.
WINDOWS_SERIAL_MAX_RESPONSE = 8192


def default_max_response(url: str) -> int | None:
    """Platform default for the per-response cap: 8 KiB on Windows serial ports, the device limit elsewhere."""
    import sys

    if sys.platform == "win32" and not url.strip().lower().startswith("tcp://"):
        return WINDOWS_SERIAL_MAX_RESPONSE
    return None


def channel_index(ch: str | int) -> int:
    if isinstance(ch, int):
        if ch in (0, 1):
            return ch
        raise ValueError(f"channel index {ch} not 0 or 1")
    s = str(ch).strip().upper()
    if s in ("CH1", "1", "A"):
        return 0
    if s in ("CH2", "2", "B"):
        return 1
    raise ValueError(f"unknown channel {ch!r}; use 'CH1' or 'CH2'")


@dataclass
class ChannelData:
    name: str
    #: compensated codes (float, firmware compensation per core and 0..255 clamp)
    codes: np.ndarray
    volts: np.ndarray
    #: True where either raw core code was 0 or 255
    clipped: np.ndarray
    config: P.ChannelConfig
    raw_a: np.ndarray | None = None
    raw_b: np.ndarray | None = None

    @property
    def volts_per_lsb(self) -> float:
        return self.config.volts_per_lsb * self.config.probe_factor


@dataclass
class Capture:
    capture_id: int
    dt: float
    divider: int
    #: sample index (relative to the capture start) of the first returned sample
    start_sample: int
    pretrigger_samples: int
    total_samples: int
    triggered: bool
    auto_timeout: bool
    channels: dict[str, ChannelData]
    trigger: P.Trigger
    origin: str = "remote"
    acq_state: int = P.AcqState.DONE

    @property
    def n(self) -> int:
        return next(iter(self.channels.values())).volts.size if self.channels else 0

    @property
    def t(self) -> np.ndarray:
        """Seconds relative to the hardware trigger point (index pretrigger_samples), uncertainty about
        +-2 samples until bench test T5 (protocol 7.5)."""
        return (self.start_sample + np.arange(self.n) - self.pretrigger_samples) * self.dt

    @property
    def sample_rate(self) -> float:
        return 1.0 / self.dt


@dataclass
class Profile:
    info: P.InfoResp
    variant: str
    caps: list[str]

    @property
    def programmable(self) -> bool:
        return bool(self.info.caps & P.Caps.PROG_TRIGGER_POS)

    @property
    def has_generator(self) -> bool:
        return bool(self.info.caps & P.Caps.GENERATOR_DDS)

    def has(self, cap: P.Caps) -> bool:
        return bool(self.info.caps & cap)


class ScopeError(Exception):
    pass


def compensate(raw: np.ndarray, comp: int) -> np.ndarray:
    """Firmware per-core compensation (protocol 7.6 step 2): code + comp, clamped to 0..255."""
    return np.clip(raw.astype(np.int16) + comp, 0, 255)


def interleave(a: np.ndarray, b: np.ndarray, order: int = 0) -> np.ndarray:
    """Interleave per protocol 7.3. order 0: trace[2k] = B[k], trace[2k+1] = A[k]; order 1: A first."""
    out = np.empty(a.size + b.size, dtype=np.result_type(a, b))
    first, second = (b, a) if order == 0 else (a, b)
    out[0::2] = first
    out[1::2] = second
    return out


def codes_to_volts(codes: np.ndarray, cfg: P.ChannelConfig) -> np.ndarray:
    """volts = (code - zero_code) x volts_per_lsb x probe_factor  [U: T8]."""
    return (np.asarray(codes, dtype=np.float64) - cfg.zero_code) * cfg.volts_per_lsb * cfg.probe_factor


def volts_to_code(v: float, cfg: P.ChannelConfig) -> float:
    return v / (cfg.volts_per_lsb * cfg.probe_factor) + cfg.zero_code


class Scope:
    """Synchronous, thread-safe client. All methods map 1:1 to protocol commands except the convenience
    methods ``capture``, ``read_capture`` and ``acquire_average``."""

    def __init__(self, transport: Transport, max_response: int | None = None):
        """``max_response`` caps the payload size requested in one response; larger READ_RAW, READ_TRACE and
        READ_SCREEN reads are split. Useful while a firmware build cannot send large responses
        (docs/hardware-status.md 2026-10-18: responses over 5 KiB failed)."""
        self.t = transport
        self.max_response = max_response
        self.lock = threading.RLock()
        self._info: P.InfoResp | None = None
        self._chan: dict[int, P.ChannelConfig] = {}
        self._cal: dict[int, P.CalResp] = {}
        self._tb: P.Timebase | None = None
        self._trig: P.Trigger | None = None
        self.remote = False

    # --- lifecycle ---------------------------------------------------------------------------------------

    @classmethod
    def connect(cls, url: str = "auto", max_response: int | None = None, **kw: Any) -> "Scope":
        """``max_response``: None = platform default (see WINDOWS_SERIAL_MAX_RESPONSE), 0 = the device limit."""
        if max_response is None:
            max_response = default_max_response(url)
        s = cls(_connect(url, **kw), max_response=max_response or None)
        s.identify()
        return s

    def close(self) -> None:
        self.t.close()

    def __enter__(self) -> "Scope":
        return self

    def __exit__(self, *a: object) -> None:
        try:
            if self.remote:
                self.release()
        except (TransportError, DeviceError):
            pass
        self.close()

    def _req(self, cmd: P.Cmd, payload: bytes = b"", timeout: float | None = None) -> bytes:
        with self.lock:
            if self._info is not None:
                need = P.CMD_CAP.get(cmd)
                if need is not None and not (self._info.caps & need):
                    raise DeviceError(cmd, P.Status.E_UNSUPPORTED, 0, f"capability {need.name} not reported")
            return self.t.request(cmd, payload, timeout).payload

    def invalidate(self) -> None:
        self._chan.clear()
        self._tb = None
        self._trig = None

    # --- system ------------------------------------------------------------------------------------------

    def identify(self) -> P.InfoResp:
        info = P.InfoResp.unpack(self._req(P.Cmd.GET_INFO))
        if info.proto_major != P.PROTO_MAJOR:
            raise TransportError(f"device speaks protocol {info.proto_major}.{info.proto_minor}, host speaks "
                                 f"{P.PROTO_MAJOR}.x; update the host package or the firmware")
        self._info = info
        return info

    @property
    def info(self) -> P.InfoResp:
        """GET_INFO of the connected device (a property, cached from connect; ``identify()`` re-reads it)."""
        if self._info is None:
            self.identify()
        assert self._info is not None
        return self._info

    def profile(self) -> Profile:
        i = self.info
        try:
            variant = P.VARIANT_NAMES[P.FpgaVariant(i.fpga_variant)]
        except ValueError:
            variant = "unknown"
        return Profile(i, variant, P.caps_names(i.caps))

    @property
    def response_limit(self) -> int:
        lim = self.info.max_response_payload or P.MAX_RESPONSE_PAYLOAD
        return min(lim, self.max_response) if self.max_response else lim

    def ping(self, data: bytes = b"ping") -> bytes:
        return self._req(P.Cmd.PING, data)

    def status(self) -> P.StatusResp:
        st = P.StatusResp.unpack(self._req(P.Cmd.GET_STATUS))
        if self.remote and st.control != P.Control.REMOTE:
            self.remote = False
            self.invalidate()
        return st

    def battery(self) -> P.BatteryResp:
        return P.BatteryResp.unpack(self._req(P.Cmd.GET_BATTERY))

    def test_pattern(self, length: int, pattern: int = 0) -> bytes:
        return self._req(P.Cmd.TEST_PATTERN, P.TestPatternReq(length, pattern).pack(), timeout=5.0)

    def take_remote(self, watchdog_s: int = 15, keep_settings: bool = False, show_traces: bool = False,
                    ignore_dtr: bool = False) -> P.StatusResp:
        flags = (P.SetRemoteReq.KEEP_SETTINGS if keep_settings else 0) | \
                (P.SetRemoteReq.SHOW_TRACES if show_traces else 0) | (P.SetRemoteReq.IGNORE_DTR if ignore_dtr else 0)
        st = P.StatusResp.unpack(self._req(P.Cmd.SET_REMOTE, P.SetRemoteReq(1, flags, watchdog_s).pack()))
        self.remote = st.control == P.Control.REMOTE
        self.invalidate()
        return st

    def release(self, keep_settings: bool = False) -> P.StatusResp:
        flags = P.SetRemoteReq.KEEP_SETTINGS if keep_settings else 0
        st = P.StatusResp.unpack(self._req(P.Cmd.SET_REMOTE, P.SetRemoteReq(0, flags, 0).pack()))
        self.remote = False
        self.invalidate()
        return st

    # --- configuration -----------------------------------------------------------------------------------

    def get_channel(self, ch: int | str, cached: bool = True) -> P.ChannelConfig:
        c = channel_index(ch)
        if not cached or c not in self._chan:
            self._chan[c] = P.ChannelConfig.unpack(self._req(P.Cmd.GET_CHANNEL, bytes((c,))))
        return self._chan[c]

    def set_channel(self, ch: int | str, *, enable: bool | None = None, range_index: int | None = None,
                    coupling: int | None = None, dcoffset: int | None = None, offset_pwm: int | None = None,
                    invert: bool | None = None, probe_index: int | None = None) -> P.ChannelConfig:
        c = channel_index(ch)
        M = P.SetChannelReq
        r = M(channel=c)
        if enable is not None:
            r.mask |= M.M_ENABLE
            r.enable = int(enable)
        if range_index is not None:
            r.mask |= M.M_RANGE
            r.range = range_index
        if coupling is not None:
            r.mask |= M.M_COUPLING
            r.coupling = coupling
        if invert is not None:
            r.mask |= M.M_INVERT
            r.invert = int(invert)
        if probe_index is not None:
            r.mask |= M.M_PROBE
            r.probe_index = probe_index
        if dcoffset is not None:
            r.mask |= M.M_OFFSET
            r.offset_mode, r.offset = 0, dcoffset
        elif offset_pwm is not None:
            r.mask |= M.M_OFFSET
            r.offset_mode, r.offset = 1, offset_pwm
        cfg = P.ChannelConfig.unpack(self._req(P.Cmd.SET_CHANNEL, r.pack()))
        self._chan[c] = cfg
        return cfg

    def get_timebase(self, cached: bool = True) -> P.Timebase:
        if not cached or self._tb is None:
            self._tb = P.Timebase.unpack(self._req(P.Cmd.GET_TIMEBASE))
        return self._tb

    def set_timebase(self, *, divider: int | None = None, ui_index: int | None = None) -> P.Timebase:
        if (divider is None) == (ui_index is None):
            raise ValueError("give exactly one of divider or ui_index")
        req = P.SetTimebaseReq(1, divider) if divider is not None else P.SetTimebaseReq(0, ui_index or 0)
        self._tb = P.Timebase.unpack(self._req(P.Cmd.SET_TIMEBASE, req.pack()))
        return self._tb

    def get_trigger(self, cached: bool = True) -> P.Trigger:
        if not cached or self._trig is None:
            self._trig = P.Trigger.unpack(self._req(P.Cmd.GET_TRIGGER))
        return self._trig

    def set_trigger(self, *, source: int | None = None, edge: int | None = None, level: int | None = None,
                    mode: int | None = None, pretrigger_samples: int | None = None,
                    total_samples: int | None = None, holdoff: int | None = None,
                    auto_timeout: int | None = None) -> P.Trigger:
        M = P.SetTriggerReq
        r = M()
        for name, bit in (("source", M.M_SOURCE), ("edge", M.M_EDGE), ("level", M.M_LEVEL), ("mode", M.M_MODE),
                          ("pretrigger_samples", M.M_PRETRIGGER), ("total_samples", M.M_TOTAL),
                          ("holdoff", M.M_HOLDOFF), ("auto_timeout", M.M_AUTO_TIMEOUT)):
            v = locals()[name]
            if v is not None:
                r.mask |= bit
                setattr(r, name, int(v))
        if r.mask & (M.M_PRETRIGGER | M.M_TOTAL) and not (r.mask & M.M_PRETRIGGER and r.mask & M.M_TOTAL):
            # the firmware validates the pair; send the current value of the other one
            cur = self.get_trigger()
            if not r.mask & M.M_PRETRIGGER:
                r.mask |= M.M_PRETRIGGER
                r.pretrigger_samples = min(cur.pretrigger_samples, (r.total_samples - 2) & ~1)
            if not r.mask & M.M_TOTAL:
                r.mask |= M.M_TOTAL
                r.total_samples = cur.total_samples
        self._trig = P.Trigger.unpack(self._req(P.Cmd.SET_TRIGGER, r.pack()))
        return self._trig

    def get_cal(self, ch: int | str, cached: bool = True) -> P.CalResp:
        c = channel_index(ch)
        if not cached or c not in self._cal:
            self._cal[c] = P.CalResp.unpack(self._req(P.Cmd.GET_CAL, bytes((c,))))
        return self._cal[c]

    # --- generator ---------------------------------------------------------------------------------------

    def get_gen(self) -> P.Gen:
        return P.Gen.unpack(self._req(P.Cmd.GET_GEN))

    def set_gen(self, *, enable: bool | None = None, freq_hz: float | None = None,
                duty: float | None = None) -> P.Gen:
        """duty as a fraction 0..1. The firmware writes duty, then frequency, then enable."""
        r = P.SetGenReq()
        if enable is not None:
            r.mask |= P.SetGenReq.M_ENABLE
            r.enable = int(enable)
        if freq_hz is not None:
            r.mask |= P.SetGenReq.M_FREQ
            r.freq = int(round(freq_hz * 1000))
        if duty is not None:
            r.mask |= P.SetGenReq.M_DUTY
            r.duty = int(round(duty * 10000))
        return P.Gen.unpack(self._req(P.Cmd.SET_GEN, r.pack()))

    # --- acquisition -------------------------------------------------------------------------------------

    def arm(self) -> P.StatusResp:
        return P.StatusResp.unpack(self._req(P.Cmd.ARM, b"\0\0\0\0"))

    def force(self) -> P.StatusResp:
        return P.StatusResp.unpack(self._req(P.Cmd.FORCE))

    def stop(self) -> P.StatusResp:
        with self.lock:
            return P.StatusResp.unpack(self.t.control(P.Cmd.STOP).payload)

    def wait(self, timeout_s: float = 2.0, cancel: Callable[[], bool] | None = None) -> P.StatusResp:
        """WAIT (deferred). Returns the status; check ``acq_state == DONE``. Long waits are split into
        <= 10 s device requests."""
        deadline = time.monotonic() + timeout_s
        while True:
            left = max(0.0, deadline - time.monotonic())
            ms = int(min(left, 10.0) * 1000)
            with self.lock:
                resp = self.t.deferred(P.Cmd.WAIT, P.U32Req(ms).pack(), ms / 1000.0 + 1.5, cancel=cancel)
            st = P.StatusResp.unpack(resp.payload)
            if st.acq_state == P.AcqState.DONE or time.monotonic() >= deadline or \
                    (cancel is not None and cancel()) or st.acq_state not in (P.AcqState.ARMED,
                                                                                P.AcqState.TRIGGERED):
                return st

    def read_raw(self, core_mask: int = 0x0F, *, addr_mode: int = 1, address: int = 0, count: int | None = None,
                 layout: int = 0, allow_not_done: bool = False) -> tuple[P.ReadRawHeader, dict[int, np.ndarray]]:
        """READ_RAW. Returns the header and, for layout 0, {core_bit_index: uint8 array}; for layout 1,
        {channel_index: interleaved uint8 array}. Split into several requests when the response would exceed
        ``response_limit``; the returned header then describes the whole read."""
        if count is None:
            count = self.get_trigger().total_words
        ncores = bin(core_mask & 0x0F).count("1") or 1
        per = max(1, (self.response_limit - 16) // ncores)
        parts: list[dict[int, np.ndarray]] = []
        first: P.ReadRawHeader | None = None
        done = 0
        while done < count:
            n = min(per, count - done)
            req = P.ReadRawReq(core_mask, addr_mode, layout, P.ReadRawReq.ALLOW_NOT_DONE if allow_not_done else 0,
                               address + done, n)
            data = self._req(P.Cmd.READ_RAW, req.pack(), timeout=3.0)
            hdr = P.ReadRawHeader.unpack(data)
            first = first or hdr
            body = np.frombuffer(data, dtype=np.uint8, offset=16)
            out: dict[int, np.ndarray] = {}
            if layout == 0:
                cores = [c for c in range(4) if core_mask & (1 << c)]
                for i, c in enumerate(cores):
                    out[c] = body[i * hdr.count:(i + 1) * hdr.count]
            else:
                chans = [ch for ch in (0, 1) if core_mask & (3 << (2 * ch))]
                for i, ch in enumerate(chans):
                    out[ch] = body[i * 2 * hdr.count:(i + 1) * 2 * hdr.count]
            parts.append(out)
            done += n
        assert first is not None
        if len(parts) == 1:
            return first, parts[0]
        first.count = count
        return first, {k: np.concatenate([p[k] for p in parts]) for k in parts[0]}

    def read_trace(self, channels: Iterable[int | str] = ("CH1",), start: int = 0, count: int = 0,
                   reread: bool = False, no_invert: bool = False) -> tuple[P.ReadTraceHeader,
                                                                          dict[int, tuple[P.TraceChannelMeta, np.ndarray]]]:
        """READ_TRACE, split into several requests when needed (count 0 = to the end)."""
        mask = 0
        for ch in channels:
            mask |= 1 << channel_index(ch)
        chans = [c for c in (0, 1) if mask & (1 << c)]
        flags = (P.ReadTraceReq.REREAD if reread else 0) | (P.ReadTraceReq.NO_INVERT if no_invert else 0)
        per = max(1, (self.response_limit - 32 - 16 * len(chans)) // len(chans))
        hdr0: P.ReadTraceHeader | None = None
        metas: list[P.TraceChannelMeta] = []
        pieces: dict[int, list[np.ndarray]] = {c: [] for c in chans}
        pos = start
        while True:
            n = per if count == 0 else min(per, count - (pos - start))
            data = self._req(P.Cmd.READ_TRACE, P.ReadTraceReq(mask, flags, pos, n).pack(), timeout=3.0)
            hdr = P.ReadTraceHeader.unpack(data)
            if hdr0 is None:
                hdr0 = hdr
                metas = [P.TraceChannelMeta.unpack(data[32 + 16 * i: 48 + 16 * i]) for i in range(len(chans))]
                flags &= ~P.ReadTraceReq.REREAD  # rebuild once, then keep reading the same trace
            off = 32 + 16 * len(chans)
            for i, c in enumerate(chans):
                pieces[c].append(np.frombuffer(data, np.uint8, hdr.count, off + i * hdr.count))
            pos += hdr.count
            end = hdr.sample_count if count == 0 else min(hdr.sample_count, start + count)
            if pos >= end or hdr.count == 0 or hdr.capture_id != hdr0.capture_id:
                break
        assert hdr0 is not None
        hdr0.count = pos - start
        return hdr0, {c: (metas[i], np.concatenate(pieces[c])) for i, c in enumerate(chans)}

    def read_live(self, core: int, count: int = 64) -> np.ndarray:
        return np.frombuffer(self._req(P.Cmd.READ_LIVE, P.ReadLiveReq(core, count).pack()), np.uint8)

    def read_screen(self) -> np.ndarray:
        """Full framebuffer as (480, 800) uint16 RGB565 [U format, T9]."""
        rows = []
        h = self.info.screen_height or 480
        row = 0
        width = self.info.screen_width or 800
        per = max(1, min(40, (self.response_limit - 8) // (2 * width)))
        while row < h:
            n = min(per, h - row)
            data = self._req(P.Cmd.READ_SCREEN, P.ReadScreenReq(row, n).pack(), timeout=3.0)
            hdr = P.ReadScreenHeader.unpack(data)
            rows.append(np.frombuffer(data, "<u2", hdr.nrows * hdr.width, 8).reshape(hdr.nrows, hdr.width))
            row += n
        return np.vstack(rows)

    def debug_fpga(self, ops: list[tuple[int, bytes | int]]) -> bytes:
        return self._req(P.Cmd.DEBUG_FPGA, P.encode_debug_ops(ops))

    def enter_fel(self) -> None:
        self._req(P.Cmd.ENTER_FEL, P.U32Req(P.ENTER_FEL_MAGIC).pack())

    def acquire_avg_device(self, n: int, channels: Iterable[int | str], start: int, count: int,
                           per_capture_timeout_ms: int = 1000, require_trigger: bool = True,
                           cancel: Callable[[], bool] | None = None) -> tuple[P.AcquireAvgHeader, dict[int, np.ndarray]]:
        """ACQUIRE_AVG (device averaging). Returns mean *compensated codes* (float) per channel index."""
        mask = 0
        for ch in channels:
            mask |= 1 << channel_index(ch)
        req = P.AcquireAvgReq(n, mask, P.AcquireAvgReq.REQUIRE_TRIGGER if require_trigger else 0,
                              per_capture_timeout_ms, start, count)
        with self.lock:
            resp = self.t.deferred(P.Cmd.ACQUIRE_AVG, req.pack(), n * per_capture_timeout_ms / 1000 + 5, cancel=cancel)
        hdr = P.AcquireAvgHeader.unpack(resp.payload)
        chans = [c for c in (0, 1) if mask & (1 << c)]
        out = {}
        for i, c in enumerate(chans):
            arr = np.frombuffer(resp.payload, "<u2", hdr.count, 16 + i * 2 * hdr.count)
            out[c] = arr.astype(np.float64) / 256.0
        return hdr, out

    # --- convenience ---------------------------------------------------------------------------------------

    def read_capture(self, channels: Iterable[int | str] = ("CH1",), start_sample: int = 0,
                     count: int | None = None, keep_raw: bool = False, st: P.StatusResp | None = None) -> Capture:
        """Read the DONE capture through READ_RAW (both cores per channel), apply the per-core compensation on
        the host and convert to volts. ``start_sample`` / ``count`` are interleaved sample indices relative to
        the capture start (0 .. total_samples)."""
        trig = self.get_trigger()
        tb = self.get_timebase()
        total = trig.total_samples
        if count is None:
            count = total - start_sample
        if start_sample < 0 or count <= 0 or start_sample + count > total:
            raise ValueError(f"sample window {start_sample}+{count} outside 0..{total}")
        w0 = start_sample // 2
        w1 = (start_sample + count + 1) // 2
        chans = [channel_index(c) for c in channels]
        mask = 0
        for c in chans:
            mask |= 3 << (2 * c)
        hdr, cores = self.read_raw(mask, addr_mode=P.ReadRawReq.ADDR_CAPTURE, address=w0, count=w1 - w0)
        if st is None:
            st = self.status()
        order = self.info.interleave_order
        out: dict[str, ChannelData] = {}
        for c in chans:
            cfg = self.get_channel(c)
            cal = self.get_cal(c)
            a, b = cores[2 * c], cores[2 * c + 1]
            comp = interleave(compensate(a, cal.adc_a_comp), compensate(b, cal.adc_b_comp), order)
            clip_raw = interleave((a == 0) | (a == 255), (b == 0) | (b == 255), order)
            off = start_sample - 2 * w0
            codes = comp[off: off + count].astype(np.float64)
            clipped = clip_raw[off: off + count]
            volts = codes_to_volts(codes, cfg)
            if cfg.invert:
                volts = -volts  # invert is a display option; codes stay as sampled
            out[CHANNEL_NAMES[c]] = ChannelData(CHANNEL_NAMES[c], codes, volts, clipped, cfg,
                                                a.copy() if keep_raw else None, b.copy() if keep_raw else None)
        return Capture(hdr.capture_id, tb.dt, tb.divider, start_sample, trig.pretrigger_samples, total,
                       bool(st.flags & P.StatusFlags.FPGA_TRIGGERED) and not (st.flags & P.StatusFlags.AUTO_TIMEOUT),
                       bool(st.flags & P.StatusFlags.AUTO_TIMEOUT), out, trig, "remote", hdr.acq_state)

    def read_trace_capture(self, channels: Iterable[int | str] = ("CH1",)) -> Capture:
        """Read the firmware's processed trace (READ_TRACE). Works in LOCAL (the UI's last trace)."""
        hdr, data = self.read_trace(channels)
        trig = self.get_trigger(cached=False)
        out: dict[str, ChannelData] = {}
        for c, (meta, arr) in data.items():
            cfg = self.get_channel(c, cached=False)
            codes = arr.astype(np.float64)
            clipped = (arr == 0) | (arr == 255)
            out[CHANNEL_NAMES[c]] = ChannelData(CHANNEL_NAMES[c], codes, codes_to_volts(codes, cfg), clipped, cfg)
        dt = (hdr.divider + 1) / P.BASE_CLOCK_HZ
        st = self.status()
        return Capture(hdr.capture_id, dt, hdr.divider, hdr.start, hdr.hw_trigger_index, hdr.sample_count,
                       bool(st.flags & P.StatusFlags.FPGA_TRIGGERED), bool(st.flags & P.StatusFlags.AUTO_TIMEOUT),
                       out, trig, "ui" if hdr.origin == 1 else "remote")

    def capture(self, channels: Iterable[int | str] = ("CH1",), timeout_s: float = 2.0, start_sample: int = 0,
                count: int | None = None, keep_raw: bool = False,
                cancel: Callable[[], bool] | None = None) -> Capture | None:
        """ARM, WAIT, READ. Returns None if no trigger arrived within ``timeout_s`` (the capture is STOPped)."""
        self.arm()
        st = self.wait(timeout_s, cancel=cancel)
        if st.acq_state != P.AcqState.DONE:
            if st.acq_state in (P.AcqState.ARMED, P.AcqState.TRIGGERED):
                self.stop()
            return None
        return self.read_capture(channels, start_sample, count, keep_raw=keep_raw, st=st)
