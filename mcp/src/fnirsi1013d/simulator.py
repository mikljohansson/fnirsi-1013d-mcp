"""FNIRSI 1013D device simulator speaking protocol v1.0 over TCP.

It models what the host can observe through docs/protocol.md:

- FPGA variants (section 5.1): ``stock`` 0x1432 (4096-word ring, fixed 1500/3000 geometry, no generator,
  only part of the ring fresh after a capture), ``al3`` 0x1532 (8192-word ring, 16382 samples per channel,
  programmable pre-trigger/total, DDS generator), ``ef2`` 0x1632 (6144 words), ``ep4`` 0x1732.
- The sample ring (section 7.2): four 8-bit cores, one word per write clock (2 dt), core B before core A in
  time by default (``--true-order`` changes the physical order so the host can test T4 handling), core B
  offset against core A (compensated by the calibration values), trigger on core A only with 2-dt
  quantisation (7.5), auto timeout, ``trigger_addr`` and ``capture_start_word`` modular arithmetic.
- ``read_lag`` (7.4): ``--hw-read-lag`` is the true FPGA lag, ``--fw-read-lag`` the value the firmware
  applies. When they differ READ_RAW returns data shifted by the difference, like a real mis-set lag.
  READ_TRACE has the stale first byte per core (1.00o5 behaviour).
- LOCAL / REMOTE ownership (6.1) with snapshot/restore, watchdog, user tap, DTR (= TCP connection) drop.
- Deferred WAIT / ACQUIRE_AVG with control requests in between, duplicate-request cache (4.3), CRC errors,
  oversize frames, the ASCII probe (10), TEST_PATTERN, READ_SCREEN, DEBUG_FPGA register model.

Signals are defined in volts at the BNC (1x probe) per channel, e.g. ``sine:freq=10kHz,amp=0.3``,
``square:freq=1kHz,amp=1,duty=0.3``, ``decay:tau=80us,amp=0.5,period=1ms,width=200us``,
``gate:period=1ms,width=200us,high=3.3``, ``dc:v=0.2``, ``gen`` (calibration-pin loopback), ``zero``.
Noise is Gaussian, ``--noise`` LSB rms per core (default 1.0, i.e. about 2 LSB as on the real unit).

Run: ``fnirsi1013d-sim --variant al3 --port 5555 --signal decay:tau=80us``.
"""

from __future__ import annotations

import argparse
import logging
import math
import random
import select
import socket
import struct
import threading
import time
from dataclasses import dataclass, field, replace
from typing import Callable

import numpy as np

from . import protocol as P
from .units import parse_quantity

log = logging.getLogger("fnirsi1013d.sim")

# ---------------------------------------------------------------------------------------------------------
# Variants


@dataclass(frozen=True)
class VariantSpec:
    name: str
    version_word: int
    variant: P.FpgaVariant
    ring_words: int
    max_total_samples: int
    programmable: bool
    generator: bool
    #: stock FPGA: words before/after the trigger that are fresh after a capture (acqprobe, 7.5)
    fresh_before: int = 0
    fresh_after: int = 0


VARIANTS = {
    "stock": VariantSpec("stock", 0x1432, P.FpgaVariant.STOCK, 4096, 3000, False, False, 910, 2000),
    "al3": VariantSpec("al3", 0x1532, P.FpgaVariant.AL3, 8192, 16382, True, True),
    "ef2": VariantSpec("ef2", 0x1632, P.FpgaVariant.EF2, 6144, 12286, True, True),
    "ep4": VariantSpec("ep4", 0x1732, P.FpgaVariant.EP4, 6144, 12286, True, True),
}

# ---------------------------------------------------------------------------------------------------------
# Signals


@dataclass
class Signal:
    kind: str = "zero"
    params: dict[str, float] = field(default_factory=dict)

    def p(self, name: str, default: float) -> float:
        return float(self.params.get(name, default))

    @property
    def dc(self) -> float:
        """DC component removed by AC coupling (approximation: the declared offset)."""
        if self.kind == "dc":
            return self.p("v", 0.0)
        if self.kind == "gate":
            return 0.5 * (self.p("high", 3.3) + self.p("low", 0.0))
        if self.kind == "gen":
            return 1.65
        return self.p("offset", 0.0)

    def eval(self, t: np.ndarray, gen: "GenState") -> np.ndarray:
        k = self.kind
        if k == "zero":
            return np.zeros_like(t)
        if k == "dc":
            return np.full_like(t, self.p("v", 0.0))
        if k == "sine":
            f = self.p("freq", 1e3)
            return self.p("offset", 0.0) + self.p("amp", 0.3) * np.sin(2 * np.pi * f * t + self.p("phase", 0.0))
        if k == "square":
            f = self.p("freq", 1e3)
            ph = np.mod(t * f, 1.0)
            hi = ph < self.p("duty", 0.5)
            a = self.p("amp", 0.3)
            return self.p("offset", 0.0) + np.where(hi, a, -a)
        if k in ("decay", "gate"):
            period = self.p("period", 1e-3)
            width = self.p("width", 200e-6)
            ph = np.mod(t, period)
            on = ph < width
            if k == "gate":
                return np.where(on, self.p("high", 3.3), self.p("low", 0.0))
            x = ph - width
            x = np.where(on, 0.0, x)
            v = self.p("amp", 0.5) * np.exp(-x / self.p("tau", 80e-6))
            amp2 = self.p("amp2", 0.0)
            if amp2:
                v = v + amp2 * np.exp(-x / self.p("tau2", 20e-6))
            fly = self.p("flyback", 0.0)
            if fly:
                v = v + fly * np.exp(-x / self.p("flyback_tau", 0.5e-6))
            v = v + self.p("offset", 0.0)
            return np.where(on, self.p("tx", 0.0), v)
        if k == "gen":
            if not gen.enable:
                return np.zeros_like(t)
            f = gen.frequency_hz
            ph = np.mod(t * f, 1.0)
            return np.where(ph < gen.duty, self.p("high", 3.3), 0.0)
        raise ValueError(f"unknown signal kind {k!r}")


def parse_signal(spec: str) -> Signal:
    """``kind:key=val,key=val``; values accept SI units (``80us``, ``10kHz``)."""
    spec = spec.strip()
    kind, _, rest = spec.partition(":")
    kind = kind.strip().lower()
    if kind not in ("zero", "dc", "sine", "square", "decay", "gate", "gen"):
        raise ValueError(f"unknown signal kind {kind!r}; use sine, square, decay, gate, dc, gen or zero")
    params: dict[str, float] = {}
    for item in filter(None, (s.strip() for s in rest.split(","))):
        k, _, v = item.partition("=")
        params[k.strip().lower()] = parse_quantity(v.strip())
    return Signal(kind, params)


def companion_signal(sig: Signal) -> Signal:
    """Default CH2 for a CH1 signal: the TX gate for a decay, a sync square for periodic signals."""
    if sig.kind == "decay":
        return Signal("gate", {"period": sig.p("period", 1e-3), "width": sig.p("width", 200e-6), "high": 3.3})
    if sig.kind in ("sine", "square"):
        return Signal("square", {"freq": sig.p("freq", 1e3), "amp": 1.5, "offset": 1.5, "duty": 0.5})
    return Signal("zero")


# ---------------------------------------------------------------------------------------------------------
# Device state


@dataclass
class ChannelState:
    enable: int = 1
    range: int = 5
    coupling: int = 0
    invert: int = 0
    probe_index: int = 1
    dcoffset: int = 0


@dataclass
class TriggerState:
    source: int = 0
    edge: int = 0
    level: int = 128
    mode: int = 0
    pretrigger_samples: int = 1500
    total_samples: int = 3000
    holdoff: int = 0
    auto_timeout: int = 0  # 0 = firmware default


@dataclass
class GenState:
    enable: int = 0
    phase_inc: int = 0
    duty_threshold: int = 0
    gen_clock_hz: int = P.GEN_CLOCK_HZ

    @property
    def frequency_hz(self) -> float:
        return self.phase_inc * self.gen_clock_hz / 2**32

    @property
    def duty(self) -> float:
        return self.duty_threshold / 2**32


@dataclass
class Settings:
    channels: list[ChannelState] = field(default_factory=lambda: [ChannelState(), ChannelState(range=3)])
    divider: int = 19
    tpd_index: int = 24
    sr_index: int = 4
    trigger: TriggerState = field(default_factory=TriggerState)
    gen: GenState = field(default_factory=GenState)

    def copy(self) -> "Settings":
        return Settings([replace(c) for c in self.channels], self.divider, self.tpd_index, self.sr_index,
                        replace(self.trigger), replace(self.gen))


@dataclass
class Calibration:
    dc_calibration_offset: tuple[int, ...] = (1000, 1000, 1000, 1000, 1000, 1000, 1000)
    input_calibration: tuple[int, ...] = P.DEFAULT_INPUT_CALIBRATION
    adc_a_comp: int = 0
    adc_b_comp: int = 0
    dc_shift_center: int = 1000
    dc_shift_size: int = 1000
    dc_shift_value: int = 1000


@dataclass
class SimConfig:
    variant: str = "al3"
    signals: tuple[Signal, Signal] = (Signal("sine", {"freq": 10e3, "amp": 0.3}), Signal("zero"))
    noise_lsb: float = 1.0
    #: physical offset of core B against core A, codes, per channel (compensated by -offset in GET_CAL)
    core_b_offset: tuple[float, float] = (3.0, -2.0)
    hw_read_lag: int = 0
    fw_read_lag: int = 0
    true_order: str = "b_first"
    #: offset (words) between the crossing word and the latched trigger_addr (test T5)
    trigger_align_offset: int = 0
    #: multiplies the modelled acquisition time (0 = captures complete at once)
    time_scale: float = 1.0
    caps_override: int | None = None
    caps_clear: int = 0
    seed: int | None = None
    fw_version: str = "0.1.0-sim"
    serial: str = "SIM00001"
    ui_capture_period_s: float = 0.05


class Deferred:
    def __init__(self, frame: P.Frame, done_at: float, result: Callable[[], tuple[int, bytes]]):
        self.frame = frame
        self.done_at = done_at
        self.result = result


class Device:
    """The protocol state machine of the scope firmware plus a model of the FPGA."""

    def __init__(self, cfg: SimConfig | None = None, clock: Callable[[], float] = time.monotonic):
        self.cfg = cfg or SimConfig()
        self.spec = VARIANTS[self.cfg.variant]
        self.clock = clock
        self.rng = np.random.default_rng(self.cfg.seed)
        self.pyrng = random.Random(self.cfg.seed)
        self.t_boot = clock()
        self.settings = Settings()
        if not self.spec.programmable:
            self.settings.trigger.pretrigger_samples = 1500
            self.settings.trigger.total_samples = 3000
        else:
            self.settings.trigger.pretrigger_samples = 1500
            self.settings.trigger.total_samples = 3000
        if not self.spec.generator:
            # stock: fixed 1 kHz, 50 %, always driven
            self.settings.gen = GenState(1, P.gen_phase_inc(1_000_000), P.gen_threshold(5000))
        self.cal = [Calibration(adc_b_comp=-round(self.cfg.core_b_offset[0])),
                    Calibration(adc_b_comp=-round(self.cfg.core_b_offset[1]))]
        self.snapshot: Settings | None = None
        self.control = P.Control.LOCAL
        self.ui_busy = False
        self.remote_flags = 0
        self.watchdog_s = 0
        self.last_frame_at = clock()
        self.exit_reason = 0
        self.acq_state = P.AcqState.IDLE
        self.capture_id = 0
        self.ui_capture_base = 0
        self.trigger_addr = 0
        self.capture_start_word = 0
        self.fpga_triggered = False
        self.fpga_done = False
        self.auto_timeout_flag = False
        self.arm_time = clock()
        self.overload = [False, False]
        R = self.spec.ring_words
        self.ring = self.rng.integers(100, 156, size=(4, R), dtype=np.int64).astype(np.uint8)
        self._pending_capture: dict | None = None
        self.capture_divider = self.settings.divider
        self.capture_geometry = (750, 1500)
        self.relay_switches = 0
        self.relay_settle_until = 0.0
        self.deferred: Deferred | None = None
        self.last_resp: tuple[int, int, bytes] | None = None  # seq, cmd, encoded frame
        self.dtr = False
        self.fel_requested = False
        self.trace_cache: dict | None = None
        self.lock = threading.RLock()
        # FPGA register model for DEBUG_FPGA
        self._dbg_cmd = 0
        self._dbg_ptr = 0
        self._dbg_first = True
        self.log: list[str] = []
        self.arm_count = 0

    # --- capabilities -------------------------------------------------------------------------------

    @property
    def caps(self) -> int:
        if self.cfg.caps_override is not None:
            return self.cfg.caps_override
        c = P.Caps.SCREENSHOT | P.Caps.DEVICE_AVERAGE | P.Caps.DEBUG_RAW | P.Caps.LIVE_READ | P.Caps.ENTER_FEL
        c |= P.Caps.READ_LAG_UNVERIFIED | P.Caps.INTERLEAVE_UNVERIFIED | P.Caps.FORCE_TRIGGER
        if self.spec.programmable:
            c |= P.Caps.PROG_TRIGGER_POS
        if self.spec.generator:
            c |= P.Caps.GENERATOR_DDS
        return int(c) & ~self.cfg.caps_clear

    # --- time ---------------------------------------------------------------------------------------

    def now(self) -> float:
        return self.clock()

    def dt(self, divider: int | None = None) -> float:
        n = self.settings.divider if divider is None else divider
        return (n + 1) / P.BASE_CLOCK_HZ

    # --- per-channel scaling -------------------------------------------------------------------------

    def uv_q8(self, ch: int, rng: int | None = None) -> int:
        r = self.settings.channels[ch].range if rng is None else rng
        return P.uv_per_lsb_q8(self.cal[ch].input_calibration[r], r)

    def zero_q8(self, ch: int, cs: ChannelState | None = None) -> int:
        cs = cs or self.settings.channels[ch]
        return P.zero_code_q8(cs.dcoffset, self.cal[ch].dc_shift_value)

    def channel_config(self, ch: int, settings: Settings | None = None) -> P.ChannelConfig:
        s = settings or self.settings
        cs = s.channels[ch]
        cal = self.cal[ch]
        return P.ChannelConfig(
            channel=ch, enable=cs.enable, range=cs.range, hw_range=min(cs.range, 5), coupling=cs.coupling,
            invert=cs.invert, probe_index=cs.probe_index, dcoffset=cs.dcoffset,
            offset_pwm=max(0, min(2000, cal.dc_calibration_offset[cs.range] + cs.dcoffset)),
            center_cal=cal.dc_calibration_offset[cs.range],
            uv_per_lsb_q8=P.uv_per_lsb_q8(cal.input_calibration[cs.range], cs.range),
            zero_code_q8=P.zero_code_q8(cs.dcoffset, cal.dc_shift_value))

    # --- signal generation ---------------------------------------------------------------------------

    def _codes(self, ch: int, t: np.ndarray, core: str, settings: Settings,
               noise: np.ndarray | None = None) -> np.ndarray:
        """Uncompensated 8-bit codes of one core at times t. ``noise`` (LSB) defaults to fresh noise."""
        sig = self.cfg.signals[ch]
        v = sig.eval(t, settings.gen)
        cs = settings.channels[ch]
        if cs.coupling == 1:
            v = v - sig.dc
        cal = self.cal[ch]
        vpl = P.uv_per_lsb_q8(cal.input_calibration[cs.range], cs.range) / 256.0 * 1e-6
        zero = P.zero_code_q8(cs.dcoffset, cal.dc_shift_value) / 256.0
        code = zero + v / vpl
        if core == "B":
            code = code + self.cfg.core_b_offset[ch]
        if noise is None and self.cfg.noise_lsb > 0:
            noise = self.rng.normal(0.0, self.cfg.noise_lsb, size=code.shape)
        if noise is not None:
            code = code + noise
        return np.clip(np.rint(code), 0, 255).astype(np.uint8)

    _NOISE_BLOCK = 4096

    def _noise(self, seed: int, k0: int, n: int) -> np.ndarray | None:
        """Noise (4, n) for words k0..k0+n-1 of one capture, identical for every call with the same seed so the
        trigger search and the stored ring agree."""
        if self.cfg.noise_lsb <= 0:
            return None
        B = self._NOISE_BLOCK
        b0, b1 = k0 // B, (k0 + n - 1) // B
        blocks = [np.random.default_rng((seed, b + (1 << 40))).normal(0.0, self.cfg.noise_lsb, size=(4, B))
                  for b in range(b0, b1 + 1)]
        allw = np.concatenate(blocks, axis=1)
        off = k0 - b0 * B
        return allw[:, off: off + n]

    def _words(self, t0: float, k0: int, n: int, dt: float, settings: Settings, seed: int) -> np.ndarray:
        """Generate n words starting at word index k0 after time origin t0. Returns (4, n) uint8 in
        core_mask order CH1A, CH1B, CH2A, CH2B."""
        k = np.arange(k0, k0 + n, dtype=np.float64)
        if self.cfg.true_order == "b_first":
            tb = t0 + (2 * k) * dt
            ta = tb + dt
        else:
            ta = t0 + (2 * k) * dt
            tb = ta + dt
        nz = self._noise(seed, k0, n)
        out = np.empty((4, n), dtype=np.uint8)
        for core, (ch, ab, tt) in enumerate(((0, "A", ta), (0, "B", tb), (1, "A", ta), (1, "B", tb))):
            out[core] = self._codes(ch, tt, ab, settings, None if nz is None else nz[core])
        return out

    def _find_trigger(self, a: np.ndarray, prev: int | None, trig: TriggerState) -> int:
        """Index in a of the first crossing (current sample) or -1. prev: sample before a[0]."""
        if prev is None:
            if len(a) < 2:
                return -1
            pa, ca, off = a[:-1].astype(np.int16), a[1:].astype(np.int16), 1
        else:
            pa = np.concatenate(([prev], a[:-1])).astype(np.int16)
            ca, off = a.astype(np.int16), 0
        lvl = trig.level
        if trig.edge == 0:
            hit = (pa < lvl) & (ca >= lvl)
        else:
            hit = (pa >= lvl) & (ca < lvl)
        idx = np.flatnonzero(hit)
        return int(idx[0]) + off if len(idx) else -1

    def geometry(self, settings: Settings | None = None) -> tuple[int, int, int]:
        """pretrigger_words, total_words, auto_timeout write clocks."""
        s = settings or self.settings
        tr = s.trigger
        if not self.spec.programmable:
            return 750, 1500, tr.auto_timeout or 1500
        return tr.pretrigger_samples // 2, tr.total_samples // 2, tr.auto_timeout or tr.total_samples

    def simulate_capture(self, settings: Settings, forced: bool = False) -> dict:
        """Run one capture through the FPGA model. Returns a dict with the new ring, trigger address, the
        modelled durations and flags. Normal mode without a trigger returns triggered=False, never_done."""
        R = self.spec.ring_words
        Pw, Tw, auto_to = self.geometry(settings)
        dt = self.dt(settings.divider)
        t0 = self.pyrng.uniform(0.0, 10.0)
        w0 = self.pyrng.randrange(R)
        seed = self.pyrng.randrange(1 << 62)
        tr = settings.trigger
        src = 0 if tr.source == 0 else 2
        kt = -1
        auto = tr.mode == 0
        limit = auto_to if auto else (1 << 21)
        if not forced:
            chunk = 16384
            k = Pw
            prev = None
            # first chunk includes the word before Pw so a crossing exactly at Pw is seen
            while k < Pw + limit:
                n = min(chunk, Pw + limit - k)
                tw = self._words(t0, k - 1 if prev is None else k, n + (1 if prev is None else 0), dt, settings,
                                 seed)
                a = tw[src]
                idx = self._find_trigger(a, prev, tr)
                if idx >= 0:
                    kt = (k - 1 if prev is None else k) + idx
                    break
                prev = int(a[-1])
                k += n
        res: dict = {"t0": t0, "dt": dt, "divider": settings.divider, "Pw": Pw, "Tw": Tw}
        if kt >= 0:
            end = kt + (Tw - Pw)
            k_first = end - R
            words = self._words(t0, k_first, R, dt, settings, seed)
            addr = (w0 + np.arange(k_first, end)) % R
            ring = self.ring.copy()
            if self.spec.fresh_before:
                lo = kt - self.spec.fresh_before
                hi = kt + self.spec.fresh_after
                sel = (np.arange(k_first, end) >= lo) & (np.arange(k_first, end) < hi)
                ring[:, addr[sel]] = words[:, sel]
                # stale part keeps the old ring contents, but the fresh window must cover the capture
                win = (np.arange(k_first, end) >= kt - Pw)
                ring[:, addr[win]] = words[:, win]
            else:
                ring[:, addr] = words
            trig_addr = (w0 + kt + self.cfg.trigger_align_offset) % R
            res.update(ring=ring, trigger_addr=trig_addr, triggered=True, auto_timeout=False,
                       t_trigger=kt * 2 * dt, t_done=end * 2 * dt)
            return res
        if auto or forced:
            # auto timeout: FPGA forces trigger_addr = pretrigger_words; data contiguous, not synchronised
            end = Pw + (auto_to if not forced else 0) + Tw
            words = self._words(t0, end - R, R, dt, settings, seed)
            ring = np.empty_like(self.ring)
            # stream word (end - Tw + a) sits at address a for a in [0, Tw); older words wrap behind
            addr = (np.arange(end - R, end) - (end - Tw)) % R
            ring[:, addr] = words
            res.update(ring=ring, trigger_addr=Pw % R, triggered=False, auto_timeout=True,
                       t_trigger=None, t_done=(Pw + (auto_to if not forced else 0) + Tw) * 2 * dt)
            return res
        res.update(ring=None, triggered=False, auto_timeout=False, t_trigger=None, t_done=None)
        return res

    # --- state machine -------------------------------------------------------------------------------

    def tick(self) -> None:
        now = self.now()
        with self.lock:
            if self.control == P.Control.REMOTE and self.watchdog_s and \
                    now - self.last_frame_at > self.watchdog_s:
                self.go_local(P.ExitReason.WATCHDOG)
            if self.control != P.Control.REMOTE:
                return
            pc = self._pending_capture
            if pc is None or self.acq_state not in (P.AcqState.ARMED, P.AcqState.TRIGGERED):
                return
            scale = self.cfg.time_scale
            if pc["t_trigger"] is not None and now >= self.arm_time + scale * pc["t_trigger"]:
                self.acq_state = P.AcqState.TRIGGERED
                self.fpga_triggered = True
            if pc["t_done"] is not None and now >= self.arm_time + scale * pc["t_done"]:
                self._complete(pc)

    def _complete(self, pc: dict) -> None:
        self.ring = pc["ring"]
        R = self.spec.ring_words
        self.trigger_addr = pc["trigger_addr"]
        self.capture_start_word = (self.trigger_addr - pc["Pw"]) % R
        self.capture_geometry = (pc["Pw"], pc["Tw"])
        self.capture_divider = pc["divider"]
        self.fpga_triggered = pc["triggered"]
        self.fpga_done = True
        self.auto_timeout_flag = pc["auto_timeout"]
        self.acq_state = P.AcqState.DONE
        self.capture_id += 1
        self.trace_cache = None
        self._pending_capture = None

    def arm(self) -> None:
        now = self.now()
        self.arm_count += 1
        if now < self.relay_settle_until:
            # firmware waits out the relay settling (8.4 step 1); in the model the ARM just starts later
            now = self.relay_settle_until
        self.arm_time = now
        self.acq_state = P.AcqState.ARMED
        self.fpga_triggered = False
        self.fpga_done = False
        self.auto_timeout_flag = False
        self._pending_capture = self.simulate_capture(self.settings)
        self.tick()

    def force(self) -> None:
        pc = self.simulate_capture(self.settings, forced=True)
        self._complete(pc)

    def go_remote(self, flags: int, watchdog_s: int) -> None:
        if self.control != P.Control.REMOTE:
            self.snapshot = self.settings.copy()
            self.control = P.Control.REMOTE
            self.acq_state = P.AcqState.IDLE
            self._pending_capture = None
        self.remote_flags = flags
        self.watchdog_s = watchdog_s
        self.last_frame_at = self.now()

    def go_local(self, reason: int, keep: bool | None = None) -> None:
        if self.control != P.Control.REMOTE:
            return
        if self.deferred is not None:
            self._abort_deferred = True
        keep = bool(self.remote_flags & P.SetRemoteReq.KEEP_SETTINGS) if keep is None else keep
        if not keep and self.snapshot is not None:
            gen_keep = self.settings.gen if not self.spec.generator else None
            self.settings = self.snapshot
            if gen_keep is not None:
                self.settings.gen = gen_keep
        self.snapshot = None
        self.control = P.Control.LOCAL
        self.acq_state = P.AcqState.IDLE
        self._pending_capture = None
        self.exit_reason = int(reason)
        self.ui_capture_base = self.capture_id
        self.ui_t0 = self.now()

    _abort_deferred = False

    def user_tap(self) -> None:
        """Simulate a tap on the REMOTE banner."""
        with self.lock:
            self.go_local(P.ExitReason.USER_TAP)

    def status(self) -> P.StatusResp:
        now = self.now()
        if self.control == P.Control.LOCAL and not self.ui_busy:
            # the UI keeps capturing
            t0 = getattr(self, "ui_t0", self.t_boot)
            self.capture_id = self.ui_capture_base + int((now - t0) / self.cfg.ui_capture_period_s)
        fl = 0
        if self.fpga_triggered:
            fl |= P.StatusFlags.FPGA_TRIGGERED
        if self.fpga_done:
            fl |= P.StatusFlags.FPGA_DONE
        if self.auto_timeout_flag:
            fl |= P.StatusFlags.AUTO_TIMEOUT
        if self.dtr:
            fl |= P.StatusFlags.DTR
        if self.deferred is not None:
            fl |= P.StatusFlags.DEFERRED_PENDING
        if self.overload[0]:
            fl |= P.StatusFlags.OVERLOAD_CH1
        if self.overload[1]:
            fl |= P.StatusFlags.OVERLOAD_CH2
        if self.control == P.Control.REMOTE and self.watchdog_s:
            wd = max(0, int(math.ceil(self.watchdog_s - (now - self.last_frame_at))))
        else:
            wd = 0xFFFF
        return P.StatusResp(
            control=int(self.control), acq_state=int(self.acq_state) if self.control == P.Control.REMOTE else 0,
            flags=int(fl), ui_busy=int(self.ui_busy), capture_id=self.capture_id & 0xFFFFFFFF,
            trigger_addr=self.trigger_addr, capture_start_word=self.capture_start_word,
            ms_since_arm=int((now - self.arm_time) * 1000) & 0xFFFFFFFF,
            uptime_ms=int((now - self.t_boot) * 1000) & 0xFFFFFFFF, watchdog_remaining_s=wd,
            battery_level=18, exit_reason=self.exit_reason)

    def info(self) -> P.InfoResp:
        sp = self.spec
        return P.InfoResp(
            fpga_version=sp.version_word, fpga_variant=int(sp.variant), ring_words=sp.ring_words,
            caps=self.caps, gen_clock_hz=P.GEN_CLOCK_HZ if sp.generator else 0,
            max_total_samples=sp.max_total_samples, min_total_samples=80 if sp.programmable else 3000,
            read_lag=self.cfg.fw_read_lag, interleave_order=0, fw_version=self.cfg.fw_version.encode(),
            fw_build=0x51A0001, serial=self.cfg.serial.encode())

    def timebase(self) -> P.Timebase:
        s = self.settings
        return P.timebase_for_divider(s.divider, s.tpd_index, s.sr_index)

    def trigger(self) -> P.Trigger:
        tr = self.settings.trigger
        Pw, Tw, auto_to = self.geometry()
        return P.Trigger(source=tr.source, edge=tr.edge, level=tr.level, mode=tr.mode,
                         pretrigger_samples=Pw * 2, total_samples=Tw * 2, holdoff=tr.holdoff,
                         auto_timeout=auto_to, pretrigger_words=Pw, total_words=Tw)

    def gen(self) -> P.Gen:
        g = self.settings.gen
        return P.Gen(enable=g.enable, phase_inc=g.phase_inc, duty_threshold=g.duty_threshold,
                     freq_mhz_actual=P.gen_freq_mhz(g.phase_inc), duty_bp_actual=P.gen_duty_bp(g.duty_threshold))

    # --- readout --------------------------------------------------------------------------------------

    def read_ring(self, core: int, start: int, count: int, *, lag_applied: int | None = None) -> np.ndarray:
        """READ_RAW semantics: exact addresses when the firmware's read_lag equals the FPGA's."""
        R = self.spec.ring_words
        fw = self.cfg.fw_read_lag if lag_applied is None else lag_applied
        shift = fw - self.cfg.hw_read_lag
        idx = (start + shift + np.arange(count)) % R
        return self.ring[core, idx]

    def _ui_capture_if_local(self) -> None:
        if self.control == P.Control.LOCAL:
            pc = self.simulate_capture(self.settings)
            if pc["ring"] is None:
                pc = self.simulate_capture(self.settings, forced=True)
            self._complete(pc)
            self.acq_state = P.AcqState.IDLE
            self.ui_capture_base = self.capture_id
            self.ui_t0 = self.now()

    def build_trace(self, ch: int, settings: Settings) -> tuple[np.ndarray, int]:
        """Processed trace of one channel (section 7.6) with the 1.00o5 stale first byte per core."""
        Pw, Tw = self.capture_geometry
        start = self.capture_start_word
        a = self.read_ring(2 * ch, start, Tw, lag_applied=self.cfg.hw_read_lag).astype(np.int16)
        b = self.read_ring(2 * ch + 1, start, Tw, lag_applied=self.cfg.hw_read_lag).astype(np.int16)
        # stale pipeline byte: the first byte after the pointer write is off by about +-1 code
        a = a.copy()
        b = b.copy()
        a[0] = np.clip(a[0] + 1, 0, 255)
        b[0] = np.clip(b[0] - 1, 0, 255)
        raw_clip0 = (a == 0) | (b == 0)
        raw_clip255 = (a == 255) | (b == 255)
        cal = self.cal[ch]
        ac = np.clip(a + cal.adc_a_comp, 0, 255)
        bc = np.clip(b + cal.adc_b_comp, 0, 255)
        # checkfirstadc approximation: near the rails force both cores equal
        rail = (ac <= 0) | (bc <= 0) | (ac >= 255) | (bc >= 255)
        both = np.where(ac >= 128, np.maximum(ac, bc), np.minimum(ac, bc))
        ac = np.where(rail, both, ac)
        bc = np.where(rail, both, bc)
        tr = np.empty(2 * Tw, dtype=np.int16)
        tr[0::2] = bc
        tr[1::2] = ac
        flags = (1 if settings.channels[ch].invert else 0) | (2 if raw_clip0.any() else 0) | \
                (4 if raw_clip255.any() else 0)
        return tr.astype(np.uint8), flags

    # --- request dispatch -----------------------------------------------------------------------------

    def handle(self, req: P.Frame) -> P.Frame | None:
        """Process one valid request frame. Returns the response, or None when it became deferred."""
        with self.lock:
            self.last_frame_at = self.now()
            self.tick()
            if req.proto_major != P.PROTO_MAJOR:
                return self._err(req, P.Status.E_VERSION, 0, f"proto {req.proto_major} != 1")
            if self.last_resp and self.last_resp[0] == req.seq and self.last_resp[1] == req.cmd:
                cached = P.decode_frame(self.last_resp[2])
                return cached
            try:
                cmd = P.Cmd(req.cmd)
            except ValueError:
                return self._done(req, self._err(req, P.Status.E_UNKNOWN_CMD, 0, f"cmd 0x{req.cmd:04X}"))
            if self.deferred is not None and self.deferred.frame.seq == req.seq and                     self.deferred.frame.cmd == req.cmd:
                return None  # retransmission of the pending deferred request: its response follows
            if self.deferred is not None and cmd not in P.CONTROL_CMDS:
                return self._done(req, self._err(req, P.Status.E_BUSY, 0, "deferred request pending"))
            if self.ui_busy and cmd not in P.BUSY_ALLOWED:
                return self._done(req, self._err(req, P.Status.E_BUSY, 0, "UI is modal"))
            if cmd not in P.LOCAL_ALLOWED and self.control != P.Control.REMOTE:
                return self._done(req, self._err(req, P.Status.E_NOT_REMOTE, 0, "acquire REMOTE first"))
            need = P.CMD_CAP.get(cmd)
            if need is not None and not (self.caps & need):
                return self._done(req, self._err(req, P.Status.E_UNSUPPORTED, 0, f"cap {need.name} missing"))
            size = P.REQUEST_SIZES.get(cmd)
            if size is not None and len(req.payload) < size:
                return self._done(req, self._err(req, P.Status.E_BAD_LEN, 0,
                                                 f"need {size} bytes, got {len(req.payload)}"))
            handler = getattr(self, f"_cmd_{cmd.name.lower()}")
            try:
                out = handler(req)
            except _DevErr as e:
                out = self._err(req, e.status, e.detail, e.text)
            if out is None:
                return None
            return self._done(req, out)

    def _done(self, req: P.Frame, resp: P.Frame) -> P.Frame:
        enc = resp.encode()
        if len(enc) <= 256:
            self.last_resp = (req.seq, req.cmd, enc)
        else:
            self.last_resp = None
        return resp

    @staticmethod
    def _err(req: P.Frame, status: int, detail: int = 0, text: str = "") -> P.Frame:
        return P.response(req, P.error_payload(detail, text), int(status))

    @staticmethod
    def _ok(req: P.Frame, payload: bytes = b"") -> P.Frame:
        return P.response(req, payload)

    def poll_deferred(self) -> P.Frame | None:
        """Called by the connection loop; returns the deferred response when it completes."""
        with self.lock:
            self.tick()
            d = self.deferred
            if d is None:
                return None
            now = self.now()
            if self._abort_deferred:
                self._abort_deferred = False
                self.deferred = None
                return self._done(d.frame, self._err(d.frame, P.Status.E_ABORTED, 0, "aborted"))
            if d.frame.cmd == P.Cmd.WAIT and self.acq_state == P.AcqState.DONE:
                self.deferred = None
                return self._done(d.frame, self._ok(d.frame, self.status().pack()))
            if now >= d.done_at:
                self.deferred = None
                status, payload = d.result()
                return self._done(d.frame, P.response(d.frame, payload, status))
            return None

    # --- command handlers ------------------------------------------------------------------------------

    def _cmd_ping(self, req: P.Frame) -> P.Frame:
        return self._ok(req, req.payload)

    def _cmd_get_info(self, req: P.Frame) -> P.Frame:
        return self._ok(req, self.info().pack())

    def _cmd_get_status(self, req: P.Frame) -> P.Frame:
        return self._ok(req, self.status().pack())

    def _cmd_set_remote(self, req: P.Frame) -> P.Frame:
        r = P.SetRemoteReq.unpack(req.payload)
        if r.action == 1:
            if self.ui_busy:
                raise _DevErr(P.Status.E_BUSY, 0, "UI is modal")
            self.go_remote(r.flags, r.watchdog_s)
        elif r.action == 0:
            if self.control == P.Control.REMOTE:
                if r.flags & P.SetRemoteReq.KEEP_SETTINGS:
                    self.remote_flags |= P.SetRemoteReq.KEEP_SETTINGS
                self.go_local(P.ExitReason.HOST_RELEASE)
        else:
            raise _DevErr(P.Status.E_BAD_ARG, 0, f"action {r.action}")
        return self._ok(req, self.status().pack())

    def _cmd_get_battery(self, req: P.Frame) -> P.Frame:
        return self._ok(req, P.BatteryResp(18, 0, 41, 0).pack())

    def _cmd_test_pattern(self, req: P.Frame) -> P.Frame:
        r = P.TestPatternReq.unpack(req.payload)
        if r.length > P.MAX_RESPONSE_PAYLOAD:
            raise _DevErr(P.Status.E_BAD_ARG, 0, f"length {r.length} > {P.MAX_RESPONSE_PAYLOAD}")
        if r.pattern > 3:
            raise _DevErr(P.Status.E_BAD_ARG, 1, f"pattern {r.pattern}")
        return self._ok(req, P.test_pattern_bytes(r.length, r.pattern))

    def _channel_arg(self, b: int) -> int:
        if b > 1:
            raise _DevErr(P.Status.E_BAD_ARG, 0, f"channel {b} > 1")
        return b

    def _cmd_get_channel(self, req: P.Frame) -> P.Frame:
        ch = self._channel_arg(req.payload[0])
        return self._ok(req, self.channel_config(ch).pack())

    def _cmd_set_channel(self, req: P.Frame) -> P.Frame:
        r = P.SetChannelReq.unpack(req.payload)
        ch = self._channel_arg(r.channel)
        cs = self.settings.channels[ch]
        M = P.SetChannelReq
        if r.mask & M.M_RANGE and r.range > 6:
            raise _DevErr(P.Status.E_BAD_ARG, 3, f"range {r.range} > 6")
        if r.mask & M.M_COUPLING and r.coupling > 1:
            raise _DevErr(P.Status.E_BAD_ARG, 4, f"coupling {r.coupling} > 1")
        if r.mask & M.M_PROBE and r.probe_index > 6:
            raise _DevErr(P.Status.E_BAD_ARG, 6, f"probe_index {r.probe_index} > 6")
        if r.mask & M.M_OFFSET and r.offset_mode > 1:
            raise _DevErr(P.Status.E_BAD_ARG, 7, f"offset_mode {r.offset_mode} > 1")
        relay = False
        if r.mask & M.M_ENABLE:
            cs.enable = 1 if r.enable else 0
        if r.mask & M.M_RANGE:
            if min(r.range, 5) != min(cs.range, 5):
                relay = True
            cs.range = r.range
        if r.mask & M.M_COUPLING:
            if r.coupling != cs.coupling:
                relay = True
            cs.coupling = r.coupling
        if r.mask & M.M_INVERT:
            cs.invert = 1 if r.invert else 0
        if r.mask & M.M_PROBE:
            cs.probe_index = r.probe_index
        if r.mask & M.M_OFFSET:
            center = self.cal[ch].dc_calibration_offset[cs.range]
            if r.offset_mode == 1:
                if not 0 <= r.offset <= 2000:
                    raise _DevErr(P.Status.E_BAD_ARG, 8, f"offset_pwm {r.offset} not in 0..2000")
                cs.dcoffset = r.offset - center
            else:
                cs.dcoffset = r.offset
            # clamp so the PWM stays in range
            cs.dcoffset = max(-center, min(2000 - center, cs.dcoffset))
        if relay:
            self.relay_switches += 1
            self.relay_settle_until = self.now() + 0.050
        return self._ok(req, self.channel_config(ch).pack())

    def _cmd_get_timebase(self, req: P.Frame) -> P.Frame:
        return self._ok(req, self.timebase().pack())

    def _cmd_set_timebase(self, req: P.Frame) -> P.Frame:
        r = P.SetTimebaseReq.unpack(req.payload)
        s = self.settings
        if r.mode == 0:
            if r.value > 34:
                raise _DevErr(P.Status.E_BAD_ARG, 1, f"timeperdiv index {r.value} > 34")
            if r.value < P.MIN_REMOTE_TIMEPERDIV_INDEX:
                raise _DevErr(P.Status.E_UNSUPPORTED, 1, "roll/long timebases not available remotely")
            sr = P.TIME_PER_DIV_SAMPLE_RATE[r.value]
            s.divider, s.tpd_index, s.sr_index = P.SAMPLE_RATE_DIVIDERS[sr], r.value, sr
        elif r.mode == 1:
            s.divider, s.tpd_index, s.sr_index = r.value, 0xFF, 0xFF
        else:
            raise _DevErr(P.Status.E_BAD_ARG, 0, f"mode {r.mode}")
        return self._ok(req, self.timebase().pack())

    def _cmd_get_trigger(self, req: P.Frame) -> P.Frame:
        return self._ok(req, self.trigger().pack())

    def _cmd_set_trigger(self, req: P.Frame) -> P.Frame:
        r = P.SetTriggerReq.unpack(req.payload)
        M = P.SetTriggerReq
        tr = self.settings.trigger
        new = replace(tr)
        if r.mask & M.M_SOURCE:
            if r.source == 2 and not (self.caps & P.Caps.EXT_TRIGGER):
                raise _DevErr(P.Status.E_UNSUPPORTED, 0, "EXT trigger needs an FPGA with EXT_TRIGGER")
            if r.source > 2:
                raise _DevErr(P.Status.E_BAD_ARG, 0, f"source {r.source}")
            new.source = r.source
        if r.mask & M.M_EDGE:
            if r.edge > 1:
                raise _DevErr(P.Status.E_BAD_ARG, 1, f"edge {r.edge}")
            new.edge = r.edge
        if r.mask & M.M_LEVEL:
            new.level = r.level
        if r.mask & M.M_MODE:
            if r.mode > 2:
                raise _DevErr(P.Status.E_BAD_ARG, 3, f"mode {r.mode}")
            new.mode = r.mode
        if r.mask & M.M_PRETRIGGER:
            new.pretrigger_samples = r.pretrigger_samples
        if r.mask & M.M_TOTAL:
            new.total_samples = r.total_samples
        if r.mask & M.M_HOLDOFF:
            if r.holdoff and not (self.caps & P.Caps.HOLDOFF):
                raise _DevErr(P.Status.E_UNSUPPORTED, 6, "hold-off not implemented by this FPGA")
            new.holdoff = r.holdoff
        if r.mask & M.M_AUTO_TIMEOUT:
            new.auto_timeout = r.auto_timeout
        if r.mask & (M.M_PRETRIGGER | M.M_TOTAL):
            if not self.spec.programmable:
                if new.pretrigger_samples != 1500 or new.total_samples != 3000:
                    raise _DevErr(P.Status.E_UNSUPPORTED, 4, "stock FPGA: pretrigger 1500, total 3000 only")
            else:
                if new.total_samples % 2:
                    raise _DevErr(P.Status.E_BAD_ARG, 5, f"total_samples {new.total_samples} must be even")
                if new.pretrigger_samples % 2:
                    raise _DevErr(P.Status.E_BAD_ARG, 4,
                                  f"pretrigger_samples {new.pretrigger_samples} must be even")
                if not 80 <= new.total_samples <= self.spec.max_total_samples:
                    raise _DevErr(P.Status.E_BAD_ARG, 5, f"total_samples {new.total_samples} not in 80.."
                                                         f"{self.spec.max_total_samples}")
                if new.pretrigger_samples > new.total_samples - 2:
                    raise _DevErr(P.Status.E_BAD_ARG, 4, f"pretrigger {new.pretrigger_samples} > total - 2 "
                                                         f"({new.total_samples - 2})")
        self.settings.trigger = new
        return self._ok(req, self.trigger().pack())

    def _cmd_arm(self, req: P.Frame) -> P.Frame:
        self.arm()
        return self._ok(req, self.status().pack())

    def _cmd_force(self, req: P.Frame) -> P.Frame:
        if self.acq_state not in (P.AcqState.ARMED, P.AcqState.TRIGGERED):
            raise _DevErr(P.Status.E_STATE, 0, f"state {P.AcqState(self.acq_state).name}, not ARMED")
        self.force()
        return self._ok(req, self.status().pack())

    def _cmd_stop(self, req: P.Frame) -> P.Frame:
        if self.acq_state in (P.AcqState.ARMED, P.AcqState.TRIGGERED):
            self.acq_state = P.AcqState.ABORTED
            self._pending_capture = None
        if self.deferred is not None:
            self._abort_deferred = True
            self.acq_state = P.AcqState.ABORTED
        return self._ok(req, self.status().pack())

    def _cmd_wait(self, req: P.Frame) -> P.Frame | None:
        (tmo,) = struct.unpack_from("<I", req.payload)
        if tmo > 10000:
            raise _DevErr(P.Status.E_BAD_ARG, 0, f"timeout_ms {tmo} > 10000")
        if self.acq_state == P.AcqState.DONE or self.acq_state not in (P.AcqState.ARMED,
                                                                         P.AcqState.TRIGGERED):
            return self._ok(req, self.status().pack())
        self.deferred = Deferred(req, self.now() + tmo / 1000.0,
                                 lambda: (P.Status.OK, self.status().pack()))
        return None

    def _resolve_start(self, mode: int, address: int) -> int:
        R = self.spec.ring_words
        if mode == 0:
            return address % R
        if mode == 1:
            return (self.capture_start_word + address) % R
        if mode == 2:
            return (self.trigger_addr + address) % R
        raise _DevErr(P.Status.E_BAD_ARG, 1, f"addr_mode {mode}")

    def _cmd_read_raw(self, req: P.Frame) -> P.Frame:
        r = P.ReadRawReq.unpack(req.payload)
        R = self.spec.ring_words
        if not 1 <= r.core_mask <= 15:
            raise _DevErr(P.Status.E_BAD_ARG, 0, f"core_mask 0x{r.core_mask:X}")
        if r.layout > 1:
            raise _DevErr(P.Status.E_BAD_ARG, 2, f"layout {r.layout}")
        if r.layout == 1:
            for ch in (0, 1):
                bits = (r.core_mask >> (2 * ch)) & 3
                if bits not in (0, 3):
                    raise _DevErr(P.Status.E_BAD_ARG, 0, "layout 1 needs both cores of each selected channel")
        if not 1 <= r.count <= R:
            raise _DevErr(P.Status.E_BAD_ARG, 8, f"count {r.count} not in 1..{R}")
        ncores = bin(r.core_mask).count("1")
        if 16 + r.count * ncores > P.MAX_RESPONSE_PAYLOAD:
            raise _DevErr(P.Status.E_OVERFLOW, 0, f"{16 + r.count * ncores} > {P.MAX_RESPONSE_PAYLOAD}")
        if self.acq_state != P.AcqState.DONE and not (r.flags & P.ReadRawReq.ALLOW_NOT_DONE):
            raise _DevErr(P.Status.E_STATE, 0, f"state {P.AcqState(self.acq_state).name}, not DONE")
        start = self._resolve_start(r.addr_mode, r.address)
        blocks = []
        clip = [False, False]
        cores = [c for c in range(4) if r.core_mask & (1 << c)]
        data = {c: self.read_ring(c, start, r.count) for c in cores}
        for c in cores:
            if np.any((data[c] == 0) | (data[c] == 255)):
                clip[c // 2] = True
        if r.layout == 0:
            blocks = [data[c].tobytes() for c in cores]
        else:
            for ch in (0, 1):
                if r.core_mask & (3 << (2 * ch)):
                    il = np.empty(2 * r.count, dtype=np.uint8)
                    il[0::2] = data[2 * ch + 1]
                    il[1::2] = data[2 * ch]
                    blocks.append(il.tobytes())
        self.overload = clip
        hdr = P.ReadRawHeader(self.capture_id, start, R, r.count, r.core_mask, r.layout, int(self.acq_state),
                              self.cfg.fw_read_lag)
        return self._ok(req, hdr.pack() + b"".join(blocks))

    def _cmd_read_trace(self, req: P.Frame) -> P.Frame:
        r = P.ReadTraceReq.unpack(req.payload)
        if not 1 <= r.ch_mask <= 3:
            raise _DevErr(P.Status.E_BAD_ARG, 0, f"ch_mask {r.ch_mask}")
        origin = 0
        if self.control == P.Control.LOCAL:
            self._ui_capture_if_local()
            origin = 1
        elif self.acq_state != P.AcqState.DONE:
            raise _DevErr(P.Status.E_STATE, 0, f"state {P.AcqState(self.acq_state).name}, not DONE")
        Pw, Tw = self.capture_geometry
        n = 2 * Tw
        if r.start >= n:
            raise _DevErr(P.Status.E_BAD_ARG, 4, f"start {r.start} >= sample_count {n}")
        count = n - r.start if r.count == 0 else min(r.count, n - r.start)
        chans = [c for c in (0, 1) if r.ch_mask & (1 << c)]
        if 32 + 16 * len(chans) + count * len(chans) > P.MAX_RESPONSE_PAYLOAD:
            raise _DevErr(P.Status.E_OVERFLOW, 0, "trace too long; lower count")
        metas, datas = [], []
        sw_idx = -1
        for c in chans:
            tr, fl = self.build_trace(c, self.settings)
            if self.settings.channels[c].invert and not (r.flags & P.ReadTraceReq.NO_INVERT):
                tr = (255 - tr.astype(np.int16)).astype(np.uint8)
            cfg = self.channel_config(c)
            metas.append(P.TraceChannelMeta(c, cfg.range, cfg.coupling, fl, cfg.uv_per_lsb_q8, cfg.zero_code_q8,
                                            self.cal[c].adc_a_comp, self.cal[c].adc_b_comp).pack())
            datas.append(tr[r.start: r.start + count].tobytes())
            if c == self.settings.trigger.source:
                sw_idx = self._sw_trigger(tr, 2 * Pw)
        if self.settings.trigger.source not in chans:
            sw_idx = -1
        hdr = P.ReadTraceHeader(self.capture_id, n, r.start, count, 2 * Pw, sw_idx, self.capture_divider,
                                r.ch_mask, origin)
        return self._ok(req, hdr.pack() + b"".join(metas) + b"".join(datas))

    def _sw_trigger(self, tr: np.ndarray, centre: int) -> int:
        lvl = self.settings.trigger.level
        lo, hi = max(2, centre - 40), min(len(tr) - 1, centre + 40)
        for i in range(lo, hi, 2):
            p, c = int(tr[i - 2]), int(tr[i])
            if (self.settings.trigger.edge == 0 and p < lvl <= c) or (self.settings.trigger.edge == 1 and
                                                                      p >= lvl > c):
                return i
        return -1

    def _cmd_read_live(self, req: P.Frame) -> P.Frame:
        r = P.ReadLiveReq.unpack(req.payload)
        if r.core > 3:
            raise _DevErr(P.Status.E_BAD_ARG, 0, f"core {r.core}")
        if not 1 <= r.count <= 4096:
            raise _DevErr(P.Status.E_BAD_ARG, 2, f"count {r.count}")
        t = self.now() - self.t_boot + np.arange(r.count) * 0.3e-6
        codes = self._codes(r.core // 2, t, "AB"[r.core % 2], self.settings)
        return self._ok(req, codes.tobytes())

    def _cmd_acquire_avg(self, req: P.Frame) -> P.Frame | None:
        r = P.AcquireAvgReq.unpack(req.payload)
        if not 1 <= r.n <= 4096:
            raise _DevErr(P.Status.E_BAD_ARG, 0, f"n {r.n}")
        if not 1 <= r.ch_mask <= 3:
            raise _DevErr(P.Status.E_BAD_ARG, 2, f"ch_mask {r.ch_mask}")
        if r.n * r.per_capture_timeout_ms > 60000:
            raise _DevErr(P.Status.E_BAD_ARG, 4, "n x per_capture_timeout_ms > 60000")
        Pw, Tw, _ = self.geometry()
        nsamp = 2 * Tw
        count = nsamp - r.start if r.count == 0 else r.count
        if r.start + count > nsamp:
            raise _DevErr(P.Status.E_BAD_ARG, 12, f"start + count > total_samples {nsamp}")
        chans = [c for c in (0, 1) if r.ch_mask & (1 << c)]
        if 16 + 2 * count * len(chans) > P.MAX_RESPONSE_PAYLOAD:
            raise _DevErr(P.Status.E_OVERFLOW, 0, "window too long")
        acc = np.zeros((len(chans), count), dtype=np.float64)
        used = dropped = 0
        first_id = self.capture_id + 1
        total_t = 0.0
        settings = self.settings
        for _ in range(r.n):
            pc = self.simulate_capture(settings)
            if pc["ring"] is None:
                dropped += 1
                total_t += r.per_capture_timeout_ms / 1000.0
                continue
            total_t += pc["t_done"] * self.cfg.time_scale + 0.001 * self.cfg.time_scale
            if (r.flags & P.AcquireAvgReq.REQUIRE_TRIGGER) and not pc["triggered"]:
                dropped += 1
                self._complete(pc)
                continue
            self._complete(pc)
            for i, c in enumerate(chans):
                start_w = self.capture_start_word
                a = self.read_ring(2 * c, start_w, Tw).astype(np.int16) + self.cal[c].adc_a_comp
                b = self.read_ring(2 * c + 1, start_w, Tw).astype(np.int16) + self.cal[c].adc_b_comp
                il = np.empty(2 * Tw, dtype=np.int16)
                il[0::2] = np.clip(b, 0, 255)
                il[1::2] = np.clip(a, 0, 255)
                acc[i] += il[r.start: r.start + count]
            used += 1
        self.acq_state = P.AcqState.DONE if used else P.AcqState.ABORTED
        mean = acc / max(used, 1)
        payload = P.AcquireAvgHeader(used, dropped, first_id, count, r.ch_mask).pack()
        payload += b"".join(np.clip(np.rint(m * 256), 0, 65535).astype("<u2").tobytes() for m in mean)
        self.deferred = Deferred(req, self.now() + total_t, lambda: (P.Status.OK, payload))
        return None

    def _cmd_get_gen(self, req: P.Frame) -> P.Frame:
        return self._ok(req, self.gen().pack())

    def _cmd_set_gen(self, req: P.Frame) -> P.Frame:
        r = P.SetGenReq.unpack(req.payload)
        g = replace(self.settings.gen)
        if r.mask & P.SetGenReq.M_FREQ:
            inc = r.freq if r.freq_mode == 1 else P.gen_phase_inc(r.freq)
            if inc > 1 << 31:
                raise _DevErr(P.Status.E_BAD_ARG, 4, "frequency above 25 MHz")
            g.phase_inc = inc
        if r.mask & P.SetGenReq.M_DUTY:
            if r.duty_mode == 0 and r.duty > 10000:
                raise _DevErr(P.Status.E_BAD_ARG, 12, f"duty {r.duty} bp > 10000")
            g.duty_threshold = r.duty if r.duty_mode == 1 else P.gen_threshold(r.duty)
        if r.mask & P.SetGenReq.M_ENABLE:
            g.enable = 1 if r.enable else 0
        self.settings.gen = g
        return self._ok(req, self.gen().pack())

    def _cmd_get_cal(self, req: P.Frame) -> P.Frame:
        ch = self._channel_arg(req.payload[0])
        c = self.cal[ch]
        a = self.ring[2 * ch].astype(np.int64)
        b = self.ring[2 * ch + 1].astype(np.int64)
        return self._ok(req, P.CalResp(ch, c.dc_calibration_offset, c.input_calibration, c.adc_a_comp,
                                       c.adc_b_comp, c.dc_shift_center, c.dc_shift_size, c.dc_shift_value,
                                       int(a.mean()), int(b.mean())).pack())

    def _cmd_read_screen(self, req: P.Frame) -> P.Frame:
        r = P.ReadScreenReq.unpack(req.payload)
        if r.row >= 480 or r.nrows == 0 or r.row + r.nrows > 480:
            raise _DevErr(P.Status.E_BAD_ARG, 0, f"rows {r.row}+{r.nrows} outside 0..480")
        if 8 + r.nrows * 800 * 2 > P.MAX_RESPONSE_PAYLOAD:
            raise _DevErr(P.Status.E_OVERFLOW, 2, "at most 41 rows per request")
        fb = self._framebuffer()
        rows = fb[r.row: r.row + r.nrows]
        return self._ok(req, P.ReadScreenHeader(800, r.row, r.nrows, 1).pack() + rows.astype("<u2").tobytes())

    def _framebuffer(self) -> np.ndarray:
        fb = np.zeros((480, 800), dtype=np.uint16)
        fb[:, :] = 0x0000
        fb[40::50, :] = 0x4208  # grid
        fb[:, 0::80] = 0x4208
        if self.control == P.Control.REMOTE:
            fb[0:24, :] = 0x001F  # banner
        Pw, Tw = self.capture_geometry
        for ch, colour in ((0, 0xFFE0), (1, 0x07FF)):
            a = self.ring[2 * ch, (self.capture_start_word + np.arange(Tw)) % self.spec.ring_words]
            x = np.linspace(0, len(a) - 1, 800).astype(int)
            y = (479 - a[x].astype(int) * 479 // 255)
            fb[y, np.arange(800)] = colour
        return fb

    def _cmd_debug_fpga(self, req: P.Frame) -> P.Frame:
        try:
            ops = P.decode_debug_ops(req.payload)
        except P.ProtocolError as e:
            raise _DevErr(P.Status.E_BAD_ARG, 0, str(e)[:60])
        out = bytearray()
        R = self.spec.ring_words
        for op, arg in ops:
            if op == P.DBG_WRITE_CMD:
                self._dbg_cmd = int(arg)  # type: ignore[arg-type]
                self._dbg_first = True
            elif op == P.DBG_WRITE_DATA:
                data = bytes(arg)  # type: ignore[arg-type]
                val = int.from_bytes(data, "big")
                c = self._dbg_cmd
                if c == 0x1F:
                    self._dbg_ptr = val % R
                    self._dbg_first = True
                elif c == 0x0D:
                    self.settings.divider = val
                elif c == 0x50:
                    self.settings.gen.enable = val & 1
                elif c == 0x51:
                    self.settings.gen.phase_inc = val
                elif c == 0x52:
                    self.settings.gen.duty_threshold = val
            elif op == P.DBG_READ_DATA:
                n = int(arg)  # type: ignore[arg-type]
                c = self._dbg_cmd
                for _ in range(n):
                    out.append(self._dbg_read_byte(c))
        return self._ok(req, bytes(out))

    def _dbg_read_byte(self, c: int) -> int:
        R = self.spec.ring_words
        if c == 0x06:
            v = self.spec.version_word
            b = (v >> 8) & 0xFF if self._dbg_first else v & 0xFF
            self._dbg_first = not self._dbg_first
            return b
        if c == 0x05:
            return 1
        if c == 0x0A:
            return 1 if self.fpga_done else 0
        if c == 0x18:
            return 1 if self.fpga_triggered else 0
        if c == 0x14:
            v = self.trigger_addr
            b = (v >> 8) & 0x1F if self._dbg_first else v & 0xFF
            self._dbg_first = not self._dbg_first
            return b
        if 0x20 <= c <= 0x23:
            core = c - 0x20
            # FPGA model: the first strobe after a pointer write returns a stale byte (about +-1 code off),
            # every strobe returns the word at ptr - hw_read_lag and post-increments the pointer.
            val = int(self.ring[core, (self._dbg_ptr - self.cfg.hw_read_lag) % R])
            if self._dbg_first:
                val = max(0, min(255, val + 1))
                self._dbg_first = False
            self._dbg_ptr = (self._dbg_ptr + 1) % R
            return val
        if 0x24 <= c <= 0x27:
            t = np.array([self.now() - self.t_boot])
            return int(self._codes((c - 0x24) // 2, t, "AB"[(c - 0x24) % 2], self.settings)[0])
        return 0

    def _cmd_enter_fel(self, req: P.Frame) -> P.Frame:
        (magic,) = struct.unpack_from("<I", req.payload)
        if magic != P.ENTER_FEL_MAGIC:
            raise _DevErr(P.Status.E_BAD_ARG, 0, "bad magic")
        self.fel_requested = True
        return self._ok(req)

    # --- ASCII -----------------------------------------------------------------------------------------

    def handle_ascii(self, line: str) -> str:
        with self.lock:
            cmd = line.strip().upper()
            if cmd == "#VER":
                return (f"#OK,FNIRSI-1013D-AI,fw={self.cfg.fw_version},proto={P.PROTO_MAJOR}.{P.PROTO_MINOR},"
                        f"fpga=0x{self.spec.version_word:04X}")
            if cmd == "#LOCAL":
                self.go_local(P.ExitReason.HOST_RELEASE)
                return "#OK"
            return "#ERR,UNKNOWN"

    def link_down(self) -> None:
        """DTR 1->0 / TCP close."""
        with self.lock:
            self.dtr = False
            self.deferred = None
            if self.control == P.Control.REMOTE and not (self.remote_flags & P.SetRemoteReq.IGNORE_DTR):
                self.go_local(P.ExitReason.DTR_DROP)


class _DevErr(Exception):
    def __init__(self, status: int, detail: int = 0, text: str = ""):
        super().__init__(text)
        self.status = status
        self.detail = detail
        self.text = text


# ---------------------------------------------------------------------------------------------------------
# TCP server


class SimServer:
    """Serves one Device over TCP; one host connection at a time (a newer connection replaces the older one,
    like a USB re-enumeration)."""

    def __init__(self, device: Device, host: str = "127.0.0.1", port: int = 5555):
        self.device = device
        self._lsock = socket.create_server((host, port))
        self.host, self.port = self._lsock.getsockname()[:2]
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        #: test hook: corrupt the next n response frames (flip a payload byte)
        self.corrupt_next = 0
        #: test hook: send garbage before the next response
        self.garbage_next = b""
        #: test hook: drop (do not send) the next n responses
        self.drop_next = 0

    @property
    def url(self) -> str:
        return f"tcp://{self.host}:{self.port}"

    def start(self) -> "SimServer":
        self._thread = threading.Thread(target=self.serve_forever, name="fnirsi-sim", daemon=True)
        self._thread.start()
        return self

    def stop(self) -> None:
        self._stop.set()
        try:
            self._lsock.close()
        except OSError:
            pass
        if self._thread:
            self._thread.join(timeout=2)

    def __enter__(self) -> "SimServer":
        return self.start()

    def __exit__(self, *a: object) -> None:
        self.stop()

    def serve_forever(self) -> None:
        client: socket.socket | None = None
        parser = P.FrameParser(max_payload=P.MAX_REQUEST_PAYLOAD, ascii_lines=True, max_ascii=63)
        last_rx = time.monotonic()
        while not self._stop.is_set():
            socks = [self._lsock] + ([client] if client else [])
            try:
                r, _, _ = select.select(socks, [], [], 0.002)
            except (OSError, ValueError):
                if self._stop.is_set():
                    break
                r = []
            if self._lsock in r:
                try:
                    new, _ = self._lsock.accept()
                except OSError:
                    break
                new.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                if client:
                    self._close_client(client)
                client = new
                parser.reset()
                with self.device.lock:
                    self.device.dtr = True
                log.info("host connected")
            if client and client in r:
                try:
                    data = client.recv(65536)
                except OSError:
                    data = b""
                if not data:
                    self._close_client(client)
                    client = None
                    continue
                last_rx = time.monotonic()
                for ev in parser.feed(data):
                    self._event(client, ev)
                if self.device.fel_requested:
                    self.device.fel_requested = False
                    log.info("ENTER_FEL: closing the link")
                    self._close_client(client)
                    client = None
                    continue
            elif parser.pending_bytes and time.monotonic() - last_rx > P.INTERBYTE_TIMEOUT_S:
                parser.reset()
            if client:
                resp = self.device.poll_deferred()
                if resp is not None:
                    self._send(client, resp)
            else:
                self.device.tick()
        if client:
            self._close_client(client)

    def _close_client(self, client: socket.socket) -> None:
        try:
            client.close()
        except OSError:
            pass
        self.device.link_down()
        log.info("host disconnected")

    def _event(self, client: socket.socket, ev: P.ParseEvent) -> None:
        dev = self.device
        if isinstance(ev, P.Frame):
            if ev.is_response:
                return
            resp = dev.handle(ev)
            if resp is not None:
                self._send(client, resp)
        elif isinstance(ev, P.BadPayloadCrc):
            with dev.lock:
                dev.last_frame_at = dev.now()
            self._send(client, P.response(ev.frame, P.error_payload(0, "payload crc"), P.Status.E_BAD_CRC))
        elif isinstance(ev, P.Oversize):
            self._send(client, P.response(ev.frame, P.error_payload(0, f"frame over {P.MAX_REQUEST_FRAME}"),
                                          P.Status.E_BAD_LEN))
        elif isinstance(ev, (P.AsciiLine, P.AsciiOverflow)):
            reply = dev.handle_ascii(ev.text) if isinstance(ev, P.AsciiLine) else "#ERR,UNKNOWN"
            try:
                client.sendall(reply.encode() + b"\n")
            except OSError:
                pass

    def _send(self, client: socket.socket, resp: P.Frame) -> None:
        if self.drop_next:
            self.drop_next -= 1
            return
        data = bytearray(resp.encode())
        if self.corrupt_next and resp.payload:
            self.corrupt_next -= 1
            data[P.HEADER_SIZE] ^= 0xFF
        if self.garbage_next:
            data = bytearray(self.garbage_next) + data
            self.garbage_next = b""
        try:
            client.sendall(bytes(data))
        except OSError:
            pass


def start_simulator(cfg: SimConfig | None = None, host: str = "127.0.0.1", port: int = 0) -> SimServer:
    """Start an in-process simulator on an ephemeral port (for tests and quick experiments)."""
    return SimServer(Device(cfg or SimConfig()), host, port).start()


# ---------------------------------------------------------------------------------------------------------
# CLI


def build_config(args: argparse.Namespace) -> SimConfig:
    ch1 = parse_signal(args.ch1 or args.signal)
    ch2 = parse_signal(args.ch2) if args.ch2 else companion_signal(ch1)
    return SimConfig(variant=args.variant, signals=(ch1, ch2), noise_lsb=args.noise,
                     hw_read_lag=args.hw_read_lag, fw_read_lag=args.fw_read_lag, true_order=args.true_order,
                     trigger_align_offset=args.trigger_align_offset, time_scale=args.time_scale,
                     seed=args.seed)


def main(argv: list[str] | None = None) -> None:
    ap = argparse.ArgumentParser(prog="fnirsi1013d-sim", description="FNIRSI 1013D protocol v1 device simulator")
    ap.add_argument("--variant", choices=sorted(VARIANTS), default="al3")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=5555)
    ap.add_argument("--signal", default="sine:freq=10kHz,amp=0.3",
                    help="CH1 signal, e.g. decay:tau=80us,amp=0.5,period=1ms,width=200us; CH2 defaults to the "
                         "matching TX gate / sync square")
    ap.add_argument("--ch1", help="CH1 signal (overrides --signal)")
    ap.add_argument("--ch2", help="CH2 signal")
    ap.add_argument("--noise", type=float, default=1.0, help="noise per core, LSB rms")
    ap.add_argument("--hw-read-lag", type=int, default=0, choices=(0, 1))
    ap.add_argument("--fw-read-lag", type=int, default=0, choices=(0, 1))
    ap.add_argument("--true-order", choices=("b_first", "a_first"), default="b_first")
    ap.add_argument("--trigger-align-offset", type=int, default=0)
    ap.add_argument("--time-scale", type=float, default=1.0,
                    help="scale modelled acquisition time (0 = instant captures)")
    ap.add_argument("--seed", type=int)
    ap.add_argument("-v", "--verbose", action="store_true")
    args = ap.parse_args(argv)
    logging.basicConfig(level=logging.INFO if args.verbose else logging.WARNING,
                        format="%(asctime)s %(name)s %(message)s")
    cfg = build_config(args)
    srv = SimServer(Device(cfg), args.host, args.port)
    print(f"fnirsi1013d-sim: variant {cfg.variant} (0x{VARIANTS[cfg.variant].version_word:04X}) on {srv.url}; "
          f"CH1 {cfg.signals[0].kind} {cfg.signals[0].params}, CH2 {cfg.signals[1].kind} {cfg.signals[1].params}",
          flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        srv.stop()


if __name__ == "__main__":
    main()
