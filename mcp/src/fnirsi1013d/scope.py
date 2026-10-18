"""High-level client for the FNIRSI 1013D: numpy arrays and physical units.

    from fnirsi1013d.scope import Scope
    with Scope.connect("tcp://localhost:5555") as s:
        s.ensure_control()                               # no-op on collaborative firmware (protocol 1.3)
        s.set_timebase(divider=9)                       # 20 MS/s
        s.set_trigger(source=1, edge=1, level=160, mode=1, pretrigger_samples=800, total_samples=16000)
        cap = s.capture(timeout_s=1.0)                   # host single capture; the scope screen shows it
        v = cap.channels["CH1"].volts                    # numpy, volts at the probe tip
        t = cap.t                                        # seconds relative to the trigger
        latest = s.read_capture(["CH1"])                 # latest capture (UI or host) with its own settings

Collaborative mode (protocol 1.3, cap SHARED_MODE): the UI and the host share one live settings set; there is
no ownership. Every capture carries the ``settings_seq`` it was taken under, so long runs can detect a change
made at the scope. Firmware without SHARED_MODE gets the old exclusive model: ``ensure_control()`` takes REMOTE
and ``end_control()`` (also on close) releases it.

Scaling [U, bench test T8, docs/protocol.md section 8.5]: the firmware reports ``uv_per_lsb_q8`` and
``zero_code_q8`` per channel, derived from ``input_calibration``, the 1x ``mul_factor`` table and the DC
shift calibration. The host computes ``volts = (code_compensated - zero_code) x volts_per_lsb x probe``.
Until T8 has been run on the owner's unit, absolute volts are an estimate (relative shapes, time constants and
frequencies are not affected).
"""

from __future__ import annotations

import threading
import time
from dataclasses import dataclass, field
from typing import Any, Callable, Iterable

import numpy as np

from . import protocol as P
from .calibration import ChannelScale, UnitCalibration, channel_scale
from .transport import DeviceError, Transport, TransportError, connect as _connect

CHANNEL_NAMES = ("CH1", "CH2")

#: Static caveats (docs/mcp-tools.md section 3).
SCALING_CAVEAT = ("Absolute volts use the per-unit calibration when one covers the channel's range (field "
                  "'scaling' = 'unit-calibration'), else the firmware's uv_per_lsb/zero_code formula, whose zero was "
                  "off by ~2 codes (and more with the trace moved) on the owner's unit (T8). scope_calibrate_volts "
                  "with a known DC voltage fixes the zero per range and ADC core.")


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
    #: the conversion used for ``volts`` (per-unit calibration or the firmware formula)
    scale: ChannelScale | None = None

    @property
    def volts_per_lsb(self) -> float:
        if self.scale is not None:
            return self.scale.volts_per_lsb
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
    #: protocol 1.3: settings_seq the capture was started under (None on older firmware)
    settings_seq: int | None = None
    #: protocol 1.3: GET_CAPTURE_INFO of this capture when it was read from the device
    capture_info: P.CaptureInfo | None = None
    #: what the client had to do to read it (e.g. pausing RUN), for caveats
    notes: list[str] = field(default_factory=list)

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
class Spectrum:
    """The spectrum the scope's spectrum view shows (READ_SPECTRUM, protocol 1.7)."""

    header: P.SpectrumHeader
    #: levels in centi-dBV (centi-dBA in A mode), P.SPECTRUM_EMPTY = empty bin
    cdb: np.ndarray

    @property
    def n_fft(self) -> int:
        return 1 << self.header.log2n

    @property
    def sample_rate(self) -> float:
        """Exact rate from the divider (the header's Hz field is rounded)."""
        return P.BASE_CLOCK_HZ / (self.header.divider + 1)

    @property
    def bin_width(self) -> float:
        return self.sample_rate / self.n_fft

    @property
    def enbw_bins(self) -> float:
        return self.header.enbw_milli / 1000.0

    @property
    def freqs(self) -> np.ndarray:
        return (self.header.first + np.arange(self.cdb.size)) * self.bin_width

    @property
    def db(self) -> np.ndarray:
        """Levels in dB (NaN for empty bins)."""
        out = self.cdb.astype(np.float64) / 100.0
        out[self.cdb <= P.SPECTRUM_EMPTY] = np.nan
        return out

    @property
    def db_codes(self) -> np.ndarray:
        """Levels in dB re 1 code RMS (the device's gain removed)."""
        return self.db - self.header.gain_cdb / 100.0

    @property
    def unit(self) -> str:
        return "dBA" if self.header.flags & P.SpectrumHeader.F_AMPS else "dBV"


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


class CaptureChanged(ScopeError):
    """A newer capture replaced the one being read (RUN mode, chunked read)."""


@dataclass
class CaptureContext:
    """Settings verified to belong to ``settings_seq`` (protocol 1.3). Captures whose capture_settings_seq equals
    it can be converted without GET_CAPTURE_INFO."""

    settings_seq: int
    trigger: P.Trigger
    timebase: P.Timebase
    channels: dict[int, P.ChannelConfig]

    def settings_key(self) -> dict[str, Any]:
        """Same keys as ``CaptureInfo.settings_key`` (for reporting what changed)."""
        tr = self.trigger
        d: dict[str, Any] = {"divider": self.timebase.divider, "trig_source": tr.source, "trig_edge": tr.edge,
                             "trig_level": tr.level, "trig_mode": tr.mode,
                             "pretrigger_samples": tr.pretrigger_samples, "total_samples": tr.total_samples}
        for i, c in sorted(self.channels.items()):
            for k in ("enable", "range", "coupling", "invert", "probe_index", "offset_pwm"):
                d[f"CH{i + 1}.{k}"] = getattr(c, k)
        return d


def settings_diff(old: dict[str, Any], new: dict[str, Any]) -> dict[str, list[Any]]:
    """{key: [old, new]} for every setting that differs."""
    return {k: [old.get(k), new.get(k)] for k in sorted(set(old) | set(new)) if old.get(k) != new.get(k)}


ORIGIN_NAMES = {P.CaptureOrigin.HOST: "host", P.CaptureOrigin.UI_RUN: "ui_run", P.CaptureOrigin.UI_SINGLE: "ui_single"}


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
        #: legacy (no SHARED_MODE) only: REMOTE held by this client
        self.remote = False
        #: protocol 1.3: settings_seq the caches belong to (None: unknown)
        self._cache_seq: int | None = None
        #: last status-shaped answer
        self.last_status: P.StatusResp | None = None
        #: per-unit, per-core voltage calibration (bench test T8); None = the firmware formula
        self.unit_cal: UnitCalibration | None = None

    # --- lifecycle ---------------------------------------------------------------------------------------

    @classmethod
    def connect(cls, url: str = "auto", max_response: int | None = None, **kw: Any) -> "Scope":
        """``max_response``: optional cap on one response payload (larger reads are split); None or 0 = the
        device limit (``max_response_payload``). The 8 KiB Windows default was dropped after the fw 0.1.6/0.1.7
        RX fix passed the census (2000 PINGs + 100 x 64 KiB, retries off, 0 errors)."""
        cal_file = kw.pop("cal_file", None)
        s = cls(_connect(url, **kw), max_response=max_response or None)
        s.identify()
        if cal_file:
            s.unit_cal = UnitCalibration.load(cal_file)
        return s

    def close(self) -> None:
        self.t.close()

    def __enter__(self) -> "Scope":
        return self

    def __exit__(self, *a: object) -> None:
        try:
            self.end_control()
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
        self._cache_seq = None

    def _status(self, payload: bytes) -> P.StatusResp:
        """Decode a status-shaped answer and keep the caches consistent with the device's settings_seq."""
        st = P.StatusResp.unpack(payload)
        self.last_status = st
        if st.shared:
            if self._cache_seq != st.settings_seq:
                self.invalidate()
                self._cache_seq = st.settings_seq
        elif self.remote and st.control != P.Control.REMOTE:
            self.remote = False
            self.invalidate()
        return st

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

    @property
    def shared(self) -> bool:
        """Collaborative mode (protocol 1.3, cap SHARED_MODE): no LOCAL/REMOTE ownership."""
        return bool(self.info.caps & P.Caps.SHARED_MODE)

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
        return self._status(self._req(P.Cmd.GET_STATUS))

    def link_stats(self) -> dict[str, int] | None:
        """Device-side link counters (GET_LINK_STATS, protocol >= 1.2), or None on older firmware."""
        if self.info.proto_minor < 2:
            return None
        return P.parse_link_stats(self._req(P.Cmd.GET_LINK_STATS))

    def event_log(self, rearm: bool = False) -> P.EventLog | None:
        """GET_EVENT_LOG (protocol >= 1.5): the last 64/128 USB/link events with device ms, or None on older
        firmware. ``rearm`` (fw 0.2.18) clears the reset snapshot after this read; a plain read keeps it."""
        if self.info.proto_minor < 5:
            return None
        return P.parse_event_log(self._req(P.Cmd.GET_EVENT_LOG, bytes([P.EVENT_LOG_REARM]) if rearm else b""))

    def battery(self) -> P.BatteryResp:
        return P.BatteryResp.unpack(self._req(P.Cmd.GET_BATTERY))

    def test_pattern(self, length: int, pattern: int = 0) -> bytes:
        return self._req(P.Cmd.TEST_PATTERN, P.TestPatternReq(length, pattern).pack())

    # --- control (legacy exclusive model only) --------------------------------------------------------------

    def ensure_control(self, watchdog_s: int = 15, keep_settings: bool = False) -> bool:
        """Make SET_*/ARM/READ_* possible. Collaborative firmware: nothing to do (returns False). Legacy firmware:
        take REMOTE if not held (returns True when it did)."""
        if self.shared:
            return False
        if self.remote:
            return False
        self.legacy_take_remote(watchdog_s=watchdog_s, keep_settings=keep_settings)
        return True

    def end_control(self, keep_settings: bool = False) -> None:
        """Legacy firmware: release REMOTE if held. Collaborative firmware: nothing to do."""
        if self.remote and not self.shared:
            self.legacy_release(keep_settings=keep_settings)

    def legacy_take_remote(self, watchdog_s: int = 15, keep_settings: bool = False, show_traces: bool = False,
                           ignore_dtr: bool = False) -> P.StatusResp:
        """SET_REMOTE acquire (protocol <= 1.2 exclusive model; a no-op on SHARED_MODE firmware)."""
        flags = (P.SetRemoteReq.KEEP_SETTINGS if keep_settings else 0) | \
                (P.SetRemoteReq.SHOW_TRACES if show_traces else 0) | (P.SetRemoteReq.IGNORE_DTR if ignore_dtr else 0)
        st = self._status(self._req(P.Cmd.SET_REMOTE, P.SetRemoteReq(1, flags, watchdog_s).pack()))
        self.remote = st.control == P.Control.REMOTE
        self.invalidate()
        return st

    def legacy_release(self, keep_settings: bool = False) -> P.StatusResp:
        """SET_REMOTE release (protocol <= 1.2 exclusive model)."""
        flags = P.SetRemoteReq.KEEP_SETTINGS if keep_settings else 0
        st = self._status(self._req(P.Cmd.SET_REMOTE, P.SetRemoteReq(0, flags, 0).pack()))
        self.remote = False
        self.invalidate()
        return st

    # --- collaborative mode (protocol 1.3) --------------------------------------------------------------

    def set_acq_state(self, run: bool) -> P.StatusResp:
        """SET_ACQ_STATE: RUN (the UI acquires continuously) or STOP. A pending host single is aborted."""
        state = P.RunState.RUN if run else P.RunState.STOP
        return self._status(self._req(P.Cmd.SET_ACQ_STATE, P.SetAcqStateReq(int(state)).pack()))

    def capture_info(self) -> P.CaptureInfo:
        """GET_CAPTURE_INFO: the settings the latest finished capture (any origin) was taken with."""
        return P.CaptureInfo.unpack(self._req(P.Cmd.GET_CAPTURE_INFO))

    def capture_context(self, attempts: int = 5) -> CaptureContext:
        """Read the live trigger, timebase and both channels and prove they belong to one settings_seq (status
        before and after agree). Legacy firmware: settings_seq 0, the settings cannot change under REMOTE."""
        for _ in range(attempts):
            st0 = self.status()
            ctx = CaptureContext(st0.settings_seq, self.get_trigger(cached=False), self.get_timebase(cached=False),
                                 {c: self.get_channel(c, cached=False) for c in (0, 1)})
            if not self.shared:
                return ctx
            st1 = self.status()
            if st1.settings_seq == st0.settings_seq:
                self._chan.update(ctx.channels)
                self._trig, self._tb = ctx.trigger, ctx.timebase
                return ctx
        raise ScopeError(f"settings kept changing while being read ({attempts} attempts); is someone turning a "
                         "knob on the scope?")

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

    # --- SD card (fw >= 0.2.2, cap SD_SECTORS) -------------------------------------------------------------

    def sd_card(self):
        from .sdcard import SdCard

        if not self.info.caps & P.Caps.SD_SECTORS:
            raise DeviceError(P.Cmd.SD_INFO, P.Status.E_UNSUPPORTED, 0, "capability SD_SECTORS not reported "
                              "(firmware >= 0.2.2)")
        return SdCard(self.t)

    def sd_info(self, verify: bool = False) -> P.SdInfo:
        """SD_INFO: card size, partition start, write window, eGON headers at 16, 80, 750 (VERIFY: checksums)."""
        with self.lock:
            return self.sd_card().info(verify)

    def sd_read(self, sector: int, count: int) -> bytes:
        """SD_READ_SECTORS in pieces of 128, each CRC-checked."""
        with self.lock:
            return self.sd_card().read(sector, count)

    def sd_write(self, image: bytes, start_sector: int, flags: int = 0, dry_run: bool = False,
                 progress: Callable[[int, int], None] | None = None, write_sectors: int | None = None):
        """Write ``image`` from ``start_sector``: stage a piece, SD_WRITE it (``write_sectors``, default 32; stage CRC
        and the card's read-back CRC checked), repeat, then read everything back and compare. ``dry_run``: only SD_INFO and the
        firmware's window/flag rules, nothing written. Returns a WritePlan (``refusals`` when it cannot be
        written, ``timings`` per phase). Takes no backup: callers do (fnirsi1013d-flash / fnirsi1013d-sd)."""
        from .sdcard import WRITE_SECTORS, plan_write, timed

        with self.lock:
            sd = self.sd_card()
            sd.write_sectors = write_sectors or WRITE_SECTORS
            info = sd.info()
            plan = plan_write(info, start_sector, len(image), flags, sd.write_sectors)
            if dry_run or not plan.ok:
                return plan
            with timed(plan.timings, "write"):
                sd.write(start_sector, image, flags, progress)
            with timed(plan.timings, "read_compare"):
                sd.verify(start_sector, image)
            return plan

    def scale(self, ch: int | str, cfg: P.ChannelConfig | None = None) -> ChannelScale:
        """Code <-> volt conversion for a channel with ``cfg`` (default: the current settings), per ADC core: the
        per-unit calibration when it covers the range, else the firmware's uv_per_lsb / zero_code."""
        c = channel_index(ch)
        return channel_scale(c, cfg or self.get_channel(c), self.get_cal(c), self.unit_cal)

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
        """ARM. Collaborative firmware: a host single capture with the live settings (the screen shows it and
        the UI stays in STOP afterwards)."""
        return self._status(self._req(P.Cmd.ARM, b"\0\0\0\0"))

    def force(self) -> P.StatusResp:
        return self._status(self._req(P.Cmd.FORCE))

    def stop(self) -> P.StatusResp:
        with self.lock:
            return self._status(self.t.control(P.Cmd.STOP).payload)

    def wait(self, timeout_s: float = 2.0, cancel: Callable[[], bool] | None = None) -> P.StatusResp:
        """WAIT (deferred). Returns the status; check ``acq_state == DONE``. Long waits are split into
        <= 10 s device requests."""
        deadline = time.monotonic() + timeout_s
        while True:
            left = max(0.0, deadline - time.monotonic())
            ms = int(min(left, 10.0) * 1000)
            with self.lock:
                resp = self.t.deferred(P.Cmd.WAIT, P.U32Req(ms).pack(), ms / 1000.0 + 1.5, cancel=cancel)
            st = self._status(resp.payload)
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
            data = self._req(P.Cmd.READ_RAW, req.pack())
            hdr = P.ReadRawHeader.unpack(data)
            if first is not None and hdr.capture_id != first.capture_id:
                raise CaptureChanged(f"capture {first.capture_id} was replaced by {hdr.capture_id} during a chunked "
                                     "READ_RAW")
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
        """READ_TRACE, split into several requests when needed (count 0 = to the end). The firmware rejects a count
        beyond the trace (E_BAD_ARG detail 8), so until the first header tells the trace length, an open-ended read
        asks for everything only when the longest possible trace fits one response, else for a short first piece."""
        mask = 0
        for ch in channels:
            mask |= 1 << channel_index(ch)
        chans = [c for c in (0, 1) if mask & (1 << c)]
        flags = (P.ReadTraceReq.REREAD if reread else 0) | (P.ReadTraceReq.NO_INVERT if no_invert else 0)
        per = max(1, (self.response_limit - 32 - 16 * len(chans)) // len(chans))
        # first-piece size for an open-ended read: the trace is normally total_samples long
        try:
            guess = self.get_trigger().total_samples
        except DeviceError:
            guess = 0
        for _ in range(3):
            try:
                got = self._read_trace_once(mask, chans, flags, per, start, count, guess)
            except DeviceError as e:
                if e.status != P.Status.E_BAD_ARG or e.detail != 8 or guess <= 1:
                    raise
                guess = 1  # the trace is shorter than the geometry says: learn its length from a short piece
                continue
            if got is not None:
                return got
            flags &= ~P.ReadTraceReq.REREAD
        raise CaptureChanged("the trace kept changing between READ_TRACE pieces (the UI captures between requests); "
                             "read it in one response (raise max_response) or stop the scope first")

    def _read_trace_once(self, mask: int, chans: list[int], flags: int, per: int, start: int, count: int,
                         guess: int):
        hdr0: P.ReadTraceHeader | None = None
        metas: list[P.TraceChannelMeta] = []
        pieces: dict[int, list[np.ndarray]] = {c: [] for c in chans}
        pos = start
        end: int | None = None if count == 0 else start + count
        while True:
            if end is not None:
                n = min(per, end - pos)
            elif (self.info.max_total_samples or 16382) - start <= per:
                n = 0  # to the end, fits whatever the trace length is
            else:
                n = min(per, max(1, guess - start))
            data = self._req(P.Cmd.READ_TRACE, P.ReadTraceReq(mask, flags, pos, n).pack())
            hdr = P.ReadTraceHeader.unpack(data)
            if hdr0 is None:
                hdr0 = hdr
                metas = [P.TraceChannelMeta.unpack(data[32 + 16 * i: 48 + 16 * i]) for i in range(len(chans))]
                flags &= ~P.ReadTraceReq.REREAD  # rebuild once, then keep reading the same trace
            elif hdr.capture_id != hdr0.capture_id:
                return None  # a newer capture replaced the trace between pieces
            off = 32 + 16 * len(chans)
            for i, c in enumerate(chans):
                pieces[c].append(np.frombuffer(data, np.uint8, hdr.count, off + i * hdr.count))
            pos += hdr.count
            end = hdr.sample_count if count == 0 else min(hdr.sample_count, start + count)
            if pos >= end or hdr.count == 0:
                break
        assert hdr0 is not None
        hdr0.count = pos - start
        return hdr0, {c: (metas[i], np.concatenate(pieces[c])) for i, c in enumerate(chans)}

    def read_spectrum(self, ch: int | str = "CH1", first: int = 0, count: int = 0) -> Spectrum:
        """READ_SPECTRUM (protocol 1.7): the spectrum the scope's spectrum view shows for ``ch``, bins
        [first, first + count) (count 0 = all that fit, which is every bin up to N = 65536). E_STATE when the view is
        off or shows no spectrum of that channel."""
        if self.info.proto_minor < 7:
            raise DeviceError(P.Cmd.READ_SPECTRUM, P.Status.E_UNKNOWN_CMD, 0, "needs protocol 1.7 (firmware >= 0.2.28)")
        h, levels = P.parse_spectrum(self._req(P.Cmd.READ_SPECTRUM,
                                               P.ReadSpectrumReq(channel_index(ch), first, count).pack()))
        return Spectrum(h, np.asarray(levels, dtype=np.int16))

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
            data = self._req(P.Cmd.READ_SCREEN, P.ReadScreenReq(row, n).pack())
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
                     count: int | None = None, keep_raw: bool = False, st: P.StatusResp | None = None,
                     ctx: CaptureContext | None = None) -> Capture:
        """Read a capture through READ_RAW (both cores per channel), apply the per-core compensation on the host
        and convert to volts. ``start_sample`` / ``count`` are interleaved sample indices relative to the capture
        start (0 .. total_samples).

        Legacy firmware: the DONE remote capture, converted with the current settings. Collaborative firmware:
        the latest finished capture of any origin (UI RUN, UI single or host single) converted with the settings
        it was taken under. When ``st`` (the WAIT answer) and ``ctx`` agree on its settings_seq, ``ctx`` is used;
        otherwise GET_CAPTURE_INFO is read. In RUN mode a chunked read can race a newer UI capture: it is retried,
        then read with RUN paused for the read (noted in ``Capture.notes``)."""
        chans = [channel_index(c) for c in channels]
        if not self.shared:
            return self._read_capture(chans, start_sample, count, keep_raw, st, self.get_trigger(),
                                      self.get_timebase(), {c: self.get_channel(c) for c in chans}, None)
        if ctx is not None and st is not None and st.capture_settings_seq == ctx.settings_seq and \
                st.capture_origin == P.CaptureOrigin.HOST:
            try:
                return self._read_capture(chans, start_sample, count, keep_raw, st, ctx.trigger, ctx.timebase,
                                          ctx.channels, None, expect_id=st.capture_id)
            except CaptureChanged:
                pass
        for _ in range(3):
            ci = self.capture_info()
            try:
                return self._from_info(ci, chans, start_sample, count, keep_raw)
            except CaptureChanged:
                continue
        # RUN mode keeps replacing the capture: pause it for one read
        self.set_acq_state(False)
        try:
            ci = self.capture_info()
            cap = self._from_info(ci, chans, start_sample, count, keep_raw)
            cap.notes.append("RUN was paused briefly to read a consistent capture (it kept being replaced)")
            return cap
        finally:
            self.set_acq_state(True)

    def _from_info(self, ci: P.CaptureInfo, chans: list[int], start_sample: int, count: int | None,
                   keep_raw: bool) -> Capture:
        h = ci.header
        cfgs = {c: ci.channel_config(c) for c in chans}
        st = P.StatusResp(acq_state=P.AcqState.DONE, capture_id=h.capture_id, capture_settings_seq=h.settings_seq,
                          capture_origin=h.origin,
                          flags=(P.StatusFlags.FPGA_TRIGGERED if h.flags & h.F_TRIGGERED else 0) |
                                (P.StatusFlags.AUTO_TIMEOUT if h.flags & h.F_AUTO_TIMEOUT else 0))
        return self._read_capture(chans, start_sample, count, keep_raw, st, ci.trigger(), ci.timebase(), cfgs, ci,
                                  expect_id=h.capture_id)

    def _read_capture(self, chans: list[int], start_sample: int, count: int | None, keep_raw: bool,
                      st: P.StatusResp | None, trig: P.Trigger, tb: P.Timebase, cfgs: dict[int, P.ChannelConfig],
                      ci: P.CaptureInfo | None, expect_id: int | None = None) -> Capture:
        total = trig.total_samples
        if count is None:
            count = total - start_sample
        if start_sample < 0 or count <= 0 or start_sample + count > total:
            raise ValueError(f"sample window {start_sample}+{count} outside 0..{total}")
        w0 = start_sample // 2
        w1 = (start_sample + count + 1) // 2
        mask = 0
        for c in chans:
            mask |= 3 << (2 * c)
        hdr, cores = self.read_raw(mask, addr_mode=P.ReadRawReq.ADDR_CAPTURE, address=w0, count=w1 - w0)
        if expect_id is not None and hdr.capture_id != expect_id:
            raise CaptureChanged(f"expected capture {expect_id}, READ_RAW returned {hdr.capture_id}")
        if st is None:
            st = self.status()
        order = self.info.interleave_order
        out: dict[str, ChannelData] = {}
        for c in chans:
            cfg = cfgs[c]
            cal = self.get_cal(c)
            a, b = cores[2 * c], cores[2 * c + 1]
            comp = interleave(compensate(a, cal.adc_a_comp), compensate(b, cal.adc_b_comp), order)
            clip_raw = interleave((a == 0) | (a == 255), (b == 0) | (b == 255), order)
            off = start_sample - 2 * w0
            codes = comp[off: off + count].astype(np.float64)
            clipped = clip_raw[off: off + count]
            sc = self.scale(c, cfg)
            volts = interleave(sc.volts_raw(a, "A"), sc.volts_raw(b, "B"), order)[off: off + count]
            if cfg.invert:
                volts = -volts  # invert is a display option; codes stay as sampled
            out[CHANNEL_NAMES[c]] = ChannelData(CHANNEL_NAMES[c], codes, volts, clipped, cfg,
                                                a.copy() if keep_raw else None, b.copy() if keep_raw else None, sc)
        shared = ci is not None or st.shared or self.shared
        origin = ORIGIN_NAMES.get(st.capture_origin, "host") if shared else "remote"
        return Capture(hdr.capture_id, tb.dt, tb.divider, start_sample, trig.pretrigger_samples, total,
                       bool(st.flags & P.StatusFlags.FPGA_TRIGGERED) and not (st.flags & P.StatusFlags.AUTO_TIMEOUT),
                       bool(st.flags & P.StatusFlags.AUTO_TIMEOUT), out, trig, origin, hdr.acq_state,
                       st.capture_settings_seq if shared else None, ci)

    def read_trace_capture(self, channels: Iterable[int | str] = ("CH1",)) -> Capture:
        """Read the firmware's processed trace (READ_TRACE). Works in LOCAL (the UI's last trace). Collaborative
        firmware: the latest capture with the settings it was taken under (GET_CAPTURE_INFO)."""
        if self.shared:
            for _ in range(3):
                ci = self.capture_info()
                hdr, data = self.read_trace(channels)
                if hdr.capture_id == ci.header.capture_id:
                    break
            else:
                raise CaptureChanged("the capture kept changing during READ_TRACE (RUN mode); stop the scope first")
            trig = ci.trigger()
            cfgs = {c: ci.channel_config(c) for c in data}
        else:
            ci = None
            hdr, data = self.read_trace(channels)
            trig = self.get_trigger(cached=False)
            cfgs = {c: self.get_channel(c, cached=False) for c in data}
        out: dict[str, ChannelData] = {}
        order = self.info.interleave_order
        for c, (meta, arr) in data.items():
            cfg = cfgs[c]
            codes = arr.astype(np.float64)
            clipped = (arr == 0) | (arr == 255)
            sc = self.scale(c, cfg)
            # trace sample i: index parity (with the start offset) says which core it came from (protocol 7.3)
            idx = hdr.start + np.arange(codes.size)
            first_is_b = order == 0
            is_b = (idx % 2 == 0) == first_is_b
            volts = np.where(is_b, sc.volts_compensated(codes, "B"), sc.volts_compensated(codes, "A"))
            out[CHANNEL_NAMES[c]] = ChannelData(CHANNEL_NAMES[c], codes, volts, clipped, cfg, scale=sc)
        dt = (hdr.divider + 1) / P.BASE_CLOCK_HZ
        if ci is not None:
            h = ci.header
            return Capture(hdr.capture_id, dt, hdr.divider, hdr.start, hdr.hw_trigger_index, hdr.sample_count,
                           bool(h.flags & h.F_TRIGGERED) and not h.flags & h.F_AUTO_TIMEOUT,
                           bool(h.flags & h.F_AUTO_TIMEOUT), out, trig, ORIGIN_NAMES.get(h.origin, "host"),
                           settings_seq=h.settings_seq, capture_info=ci)
        st = self.status()
        return Capture(hdr.capture_id, dt, hdr.divider, hdr.start, hdr.hw_trigger_index, hdr.sample_count,
                       bool(st.flags & P.StatusFlags.FPGA_TRIGGERED), bool(st.flags & P.StatusFlags.AUTO_TIMEOUT),
                       out, trig, "ui" if hdr.origin == 1 else "remote")

    def capture(self, channels: Iterable[int | str] = ("CH1",), timeout_s: float = 2.0, start_sample: int = 0,
                count: int | None = None, keep_raw: bool = False,
                cancel: Callable[[], bool] | None = None, ctx: CaptureContext | None = None) -> Capture | None:
        """ARM, WAIT, READ: one host capture with the live settings. Collaborative firmware: a host single (the
        screen shows it, the UI stays in STOP). Returns None if no trigger arrived within ``timeout_s`` (the
        capture is STOPped). ``ctx``: settings already verified for a run (saves GET_CAPTURE_INFO); check
        ``Capture.settings_seq`` against it."""
        self.arm()
        st = self.wait(timeout_s, cancel=cancel)
        if st.acq_state != P.AcqState.DONE:
            if st.acq_state in (P.AcqState.ARMED, P.AcqState.TRIGGERED):
                self.stop()
            return None
        return self.read_capture(channels, start_sample, count, keep_raw=keep_raw, st=st, ctx=ctx)
