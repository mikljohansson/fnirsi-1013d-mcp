"""FNIRSI 1013D device simulator speaking protocol v1.0 over TCP.

It models what the host can observe through docs/protocol.md:

- One FPGA bitstream (section 5.1, protocol 1.25): the firmware is built for AL3 ``P.FW_FPGA_VERSION`` (0x153C
  since protocol 1.26: 8192-word ring, 16382 samples per channel, programmable pre-trigger/total, DDS generator, EXT
  trigger, decimation filter with 16-bit samples, CIC compensation FIR, sequencer, the per-capture encode check
  (GET_HEALTH 88..103, event 21; hooks ``Device.enc_bad_next`` / ``enc_unrecovered_next``), the ADC capture
  stages (GET_HEALTH 104..123, GET_CAL 356..363, event 23; hooks ``Device.cap_counts_next``, ``cap_stored`` +
  ``cap_boot()``), and the 0x153C trigger types / hold-off (``TrigQual``, ``_search_trigger``, ``holdoff_left_s``),
  peak detect (``_peak_samples``) and hardware counter (``counter_snapshot``) of protocol 1.26).
  ``SimConfig.fpga_version`` (``--fpga-version 0x153B``) is the word the simulated FPGA reports: any other value is
  a mismatch, flagged in GET_INFO 78 / GET_HEALTH 126, and every command outside ``P.BASIC_CMDS`` answers
  E_UNSUPPORTED detail 1 with ``P.FPGA_MISMATCH_TEXT``.
- The on-device Bode plot (protocol 1.20, BODE_CTRL / READ_BODE, display mode 5) on every bitstream with the filter
  and the generator: ``SimBode`` runs the host tool's ``bode.BodeRunner`` against this device through an in-process
  link (the firmware's bode_sweep.c is a port of it), so device and host sweeps of a ``dut_in`` / ``dut_out`` DUT
  agree within noise; a host ARM during a sweep ends it (reason 5).
- The sample ring (section 7.2): four 8-bit cores, one word per write clock (2 dt), core B before core A in
  time by default (``--true-order`` changes the physical order so the host can test T4 handling), core B
  offset against core A (compensated by the calibration values), trigger on core A only with 2-dt
  quantisation (7.5), auto timeout, ``trigger_addr`` and ``capture_start_word`` modular arithmetic.
- ``read_lag`` (7.4): ``--hw-read-lag`` is the true FPGA lag, ``--fw-read-lag`` the value the firmware
  applies. When they differ READ_RAW returns data shifted by the difference, like a real mis-set lag.
  READ_TRACE has the stale first byte per core before protocol 1.22 (1.00o5 behaviour; fw 0.2.72 reads it exactly).
- Collaborative (shared) mode, protocol 1.3 (default, cap SHARED_MODE): one live settings set changed by host
  SET_* and by the simulated UI (``Device.ui_set_channel`` / ``ui_set_timebase`` / ``ui_set_trigger``,
  ``ui_press_run`` / ``ui_press_stop``), ``settings_seq``, RUN mode with UI captures every
  ``ui_capture_period_s``, host single capture (ARM), SET_ACQ_STATE, GET_CAPTURE_INFO.
- Legacy exclusive model (``--legacy``, or any caps without SHARED_MODE; protocol 1.2): LOCAL / REMOTE
  ownership (6.1) with snapshot/restore, watchdog, user tap, DTR (= TCP connection) drop.
- Deferred WAIT / ACQUIRE_AVG with control requests in between, duplicate-request cache (4.3), CRC errors,
  oversize frames, the ASCII probe (10), TEST_PATTERN, READ_SCREEN, DEBUG_FPGA register model.

Signals are defined in volts at the BNC (1x probe) per channel, e.g. ``sine:freq=10kHz,amp=0.3``,
``square:freq=1kHz,amp=1,duty=0.3``, ``decay:tau=80us,amp=0.5,period=1ms,width=200us``,
``gate:period=1ms,width=200us,high=3.3``, ``dc:v=0.2``, ``gen`` (calibration-pin loopback), ``zero``.
A device under test on the generator (Bode sweeps): ``dut_in`` / ``dut_out`` are the input and output node of an RC
low-pass (``r``, ``c``) or, with ``l`` > 0, a series RLC (output across C), driven from the calibration pin through
``rs`` (R21, 1 kOhm) with a ``high`` (3.1 V) square, seen against the probe ground (``offset`` -1.24 V); ``gain`` and
``delay`` model a channel mismatch, ``bw`` the front end (30 MHz, first order). E.g. CH1 ``dut_in:r=1.6k,c=10n``, CH2 ``dut_out:r=1.6k,c=10n``; a thru is
``dut_in`` on both.
Noise is Gaussian, ``--noise`` LSB rms per core (default 1.0, i.e. about 2 LSB as on the real unit).

Run: ``fnirsi1013d-sim --port 5555 --signal decay:tau=80us`` (``--variant al3`` is accepted and the only choice).
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
from typing import Any, Callable

import numpy as np

from . import analysis
from . import protocol as P
from .units import parse_quantity

log = logging.getLogger("fnirsi1013d.sim")

# ---------------------------------------------------------------------------------------------------------
# The bitstream (fw 0.2.102, protocol 1.25: the firmware supports FW_FPGA_VERSION only; the simulated scope is an
# AL3A10 running it unless SimConfig.fpga_version says otherwise, which models the mismatch)

#: the version word the simulated firmware is built for and the geometry that goes with it (AL3, P.FW_FPGA_VERSION)
FPGA_VERSION = P.FW_FPGA_VERSION
RING_WORDS = 8192
MAX_TOTAL_SAMPLES = 16382
#: the only --variant / SimConfig.variant value (kept so existing command lines work)
VARIANT_NAMES = ("al3",)

#: EXT trigger path latency from the pin (fpga-gen2 simulation: 20-25 ns)
EXT_LATENCY_S = 22e-9
#: decimation filter: fixed latency from pin to memory (12 write clocks) for every K and N (row 47); 25 write clocks
#: with the FIR (row 49)
DECIM_LATENCY_S = 120e-9
FIR_LATENCY_S = 250e-9
#: 0x153A+: the IOB capture and the stage pick, 40 ns more from pin to memory (4 words at the full-rate write clock)
CAP_EXTRA_LATENCY_S = 40e-9
#: decimation model: sub-samples per output period (exact for N <= this)
DECIM_SUBSAMPLES = 64
#: peak-detect model (1.26): raw sample instants evaluated per window (exact for N <= this, a uniform grid above)
PEAK_SUBSAMPLES = 1024
#: counter model (1.26): grid points per signal period for the kinds without analytic edges (decay, dut_*)
COUNTER_GRID = 1 << 16
EXT_THRESHOLD_V = 1.65

# ---------------------------------------------------------------------------------------------------------
# Signals


#: serial-bus signal kinds (mcp-soft): a piecewise-constant timeline repeated every ``period`` (serial_timeline)
SERIAL_KINDS = ("uart", "spi_clk", "spi_data", "i2c_scl", "i2c_sda")


@dataclass
class Signal:
    kind: str = "zero"
    #: numbers (SI units parsed), except ``text`` of the serial kinds, which stays a string
    params: dict[str, Any] = field(default_factory=dict)

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
        if self.kind in ("dut_in", "dut_out"):
            return self.p("offset", -1.24) + self.p("gain", 1.0) * 0.5 * self.p("high", 3.1)
        if self.kind in SERIAL_KINDS:
            times, levels, period = serial_timeline(self.kind, self.params)
            return float(np.sum(np.diff(np.append(times, period)) * levels) / period)
        return self.p("offset", 0.0)

    def eval(self, t: np.ndarray, gen: "GenState") -> np.ndarray:
        k = self.kind
        if k == "zero":
            return np.zeros_like(t)
        if k in SERIAL_KINDS:
            times, levels, period = serial_timeline(k, self.params)
            ph = np.mod(np.asarray(t, np.float64) - self.p("start", 0.0), period)
            return levels[np.searchsorted(times, ph, side="right") - 1]
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
            return np.where(gen.pin(t) > 0, self.p("high", 3.3), 0.0)
        if k in ("dut_in", "dut_out"):
            off, g = self.p("offset", -1.24), self.p("gain", 1.0)
            if not gen.enable or not gen.frequency_hz:  # a SET_PATTERN pattern is not modelled through the DUT
                return np.full_like(t, off)  # pin tri-stated: the DUT rests at its ground
            y = dut_response(np.asarray(t, np.float64) - self.p("delay", 0.0), gen.frequency_hz, gen.duty,
                             self.p("high", 3.1), *self.dut_params, node=1 if k == "dut_out" else 0,
                             bw=self.p("bw", 30e6))
            return off + g * y
        raise ValueError(f"unknown signal kind {k!r}")

    @property
    def dut_params(self) -> tuple[float, float, float, float]:
        """(rs, r, l, c) of a dut_* signal."""
        return self.p("rs", 1000.0), self.p("r", 1600.0), self.p("l", 0.0), self.p("c", 10e-9)

    def dut_h(self, f: np.ndarray | float) -> np.ndarray:
        """Analytic V_out / V_in of the dut_* network at f (Hz), probe mismatch excluded."""
        _, r, ll, c = self.dut_params
        w = 2j * np.pi * np.asarray(f, np.float64)
        return 1.0 / (1.0 + w * r * c + w * w * ll * c)


def dut_response(t: np.ndarray, f: float, duty: float, high: float, rs: float, r: float, ll: float, c: float,
                 node: int, bw: float = 30e6) -> np.ndarray:
    """Steady-state voltage (against the DUT ground) of node 0 (the DUT input, after rs) or node 1 (the output across
    c) of an RC (ll = 0: rs + r into c) or series RLC network driven by a 0 / high square of frequency f and duty
    (high while (t f mod 1) < duty, like the ``gen`` signal), seen through a first-order front end of bandwidth
    ``bw`` (0: none; the same on both channels, so it cancels in a ratio, but it keeps the square's harmonics near
    the 200 MSa/s sample rate from aliasing as they would not on the real input). Exact: the input is piecewise
    constant, so each segment is x(tau) = x_eq + e^{A tau} (x_start - x_eq), and the periodic start state solves
    (I - E_low E_high) x0 = E_low (I - E_high) x_high."""
    if ll > 0:
        A = np.array([[0.0, 1.0 / c], [-1.0 / ll, -(rs + r) / ll]])
        B = np.array([0.0, 1.0 / ll])
        C = np.array([[0.0, -rs], [1.0, 0.0]])  # V_in = u - rs i, V_out = Vc
        D = np.array([1.0, 0.0])
    else:
        tau = (rs + r) * c
        A = np.array([[-1.0 / tau]])
        B = np.array([1.0 / tau])
        C = np.array([[rs / (rs + r)], [1.0]])  # V_in = (r u + rs Vc) / (rs + r), V_out = Vc
        D = np.array([r / (rs + r), 0.0])
    if bw > 0:  # one more state: the front end's output follows the node with tau = 1 / (2 pi bw)
        k = 2 * np.pi * bw
        n0 = A.shape[0]
        A = np.block([[A, np.zeros((n0, 1))], [k * C[node][None, :], np.array([[-k]])]])
        B = np.concatenate([B, [k * D[node]]])
        sel = np.zeros(n0 + 1)
        sel[-1] = 1.0
        C = np.array([sel, sel])
        D = np.array([0.0, 0.0])
    x_hi = np.linalg.solve(A, -B * high)
    if f <= 0:
        return np.full_like(t, C[node] @ x_hi + D[node] * high)
    lam, V = np.linalg.eig(A)
    Vi = np.linalg.inv(V)

    def E(tau: float) -> np.ndarray:
        return (V * np.exp(lam * tau)) @ Vi

    T = 1.0 / f
    t1 = duty * T
    E1, E2 = E(t1), E(T - t1)
    eye = np.eye(A.shape[0])
    x0 = np.linalg.solve(eye - E2 @ E1, E2 @ (eye - E1) @ x_hi).real
    x1 = (x_hi + E1 @ (x0 - x_hi)).real
    ph = np.mod(t * f, 1.0)
    hi = ph < duty
    tau = np.where(hi, ph, ph - duty) * T
    z_hi = Vi @ (x0 - x_hi)
    z_lo = Vi @ x1
    ex = np.exp(np.outer(tau, lam))  # (n, dim)
    xs = np.where(hi[:, None], x_hi[None, :] + (ex * z_hi[None, :]) @ V.T, (ex * z_lo[None, :]) @ V.T).real
    return xs @ C[node] + np.where(hi, D[node] * high, 0.0)


#: default message of the serial kinds (``text=``): bytes with both MSB states and an odd and an even bit count
SERIAL_DEFAULT_TEXT = "Hi!"

_SERIAL_CACHE: dict[tuple, tuple[np.ndarray, np.ndarray, float]] = {}


def serial_message(params: dict[str, Any]) -> bytes:
    """The bytes a serial signal carries: ``text`` (latin-1; a CLI string, so no comma or colon) or the default."""
    text = params.get("text", SERIAL_DEFAULT_TEXT)
    return text.encode("latin-1", "replace") if isinstance(text, str) else bytes([int(text) & 0xFF])


def serial_timeline(kind: str, params: dict[str, Any]) -> tuple[np.ndarray, np.ndarray, float]:
    """Piecewise-constant line levels of a serial-bus signal over one ``period`` (default 1 ms): ``(times, levels,
    period)`` with level ``levels[i]`` from ``times[i]`` on; the message starts at time 0 and the line idles after it.

    ``uart``: one frame per message byte, back to back: start bit, ``bits`` data bits LSB first (default 8),
    ``parity`` 0 none / 1 even / 2 odd, ``stop`` stop bits (1), at ``baud`` (115200); levels ``high`` (3.3) /
    ``low`` (0), ``idle`` 1 = idle high (mark = high), 0 = inverted; ``gap`` seconds between frames (0).
    ``spi_clk`` / ``spi_data``: the message as ``bits``-bit words (8) MSB first (``msb`` 0 = LSB first) clocked at
    ``freq`` (1 MHz) with ``cpol`` / ``cpha`` (0 / 0); data changes on the non-sampling edge; ``gap`` seconds of idle
    clock between words (0; the data line rests low in a gap). No chip select (two channels).
    ``i2c_scl`` / ``i2c_sda``: START, the 7-bit ``addr`` (0x50) with the R/W bit ``rw`` (0 write), the slave's ACK
    (``nack`` 1 = no slave: NACK and STOP), the message bytes each ACKed (a read NACKs the last one), STOP, at ``freq``
    (100 kHz); SDA changes while SCL is low, a quarter period after its falling edge."""
    key = (kind, tuple(sorted((k, str(v)) for k, v in params.items())))
    hit = _SERIAL_CACHE.get(key)
    if hit is not None:
        return hit
    p = lambda name, default: float(params.get(name, default))  # noqa: E731
    high, low, period = p("high", 3.3), p("low", 0.0), p("period", 1e-3)
    msg = serial_message(params)
    seg: list[tuple[float, float]] = []  # (duration, level)
    if kind == "uart":
        tb = 1.0 / p("baud", 115200)
        nbits, parity, stop, gap = int(p("bits", 8)), int(p("parity", 0)), p("stop", 1), p("gap", 0.0)
        mark, space = (high, low) if p("idle", 1) else (low, high)
        for b in msg:
            seg.append((tb, space))
            ones = 0
            for i in range(nbits):
                bit = (b >> i) & 1
                ones += bit
                seg.append((tb, mark if bit else space))
            if parity:
                even = ones % 2 == 0
                pbit = (0 if even else 1) if parity == 1 else (1 if even else 0)  # even parity makes the total even
                seg.append((tb, mark if pbit else space))
            seg.append((stop * tb, mark))
            if gap > 0:
                seg.append((gap, mark))
        idle_level = mark
    elif kind in ("spi_clk", "spi_data"):
        half = 0.5 / p("freq", 1e6)
        nbits, cpol, cpha, msb, gap = int(p("bits", 8)), int(p("cpol", 0)), int(p("cpha", 0)), p("msb", 1), p("gap", 0.0)
        idle_clk, active_clk = (high, low) if cpol else (low, high)
        words = _spi_words(msg, nbits)
        clk: list[tuple[float, float]] = []
        dat: list[tuple[float, float]] = []
        if cpha:
            dat.append((half, low))  # the first bit appears on the first (leading) edge
        for w in words:
            for i in range(nbits):
                bit = (w >> (nbits - 1 - i)) & 1 if msb else (w >> i) & 1
                clk += [(half, idle_clk), (half, active_clk)]
                dat += [(half, high if bit else low), (half, high if bit else low)]
            if gap > 0:
                clk.append((gap, idle_clk))
                dat.append((gap, low))
        seg = clk if kind == "spi_clk" else dat
        idle_level = idle_clk if kind == "spi_clk" else low
    elif kind in ("i2c_scl", "i2c_sda"):
        q = 0.25 / p("freq", 100e3)
        addr, rw, nack = int(p("addr", 0x50)), int(p("rw", 0)), int(p("nack", 0))
        scl: list[tuple[float, float]] = []
        sda: list[tuple[float, float]] = []

        def bit_slot(level: float) -> None:  # SCL low, SDA set a quarter in, SCL high for the second half
            scl.extend([(2 * q, low), (2 * q, high)])
            sda.extend([(q, sda[-1][1] if sda else high), (3 * q, level)])

        # START: SDA falls while SCL is high, then SCL falls a quarter later
        scl.append((q, high))
        sda.append((q, low))
        frames = [((addr << 1) | (rw & 1), low if not nack else high)]
        if not nack:
            frames += [(b, high if (rw and i == len(msg) - 1) else low) for i, b in enumerate(msg)]
        for byte, ack_level in frames:
            for i in range(8):
                bit_slot(high if (byte >> (7 - i)) & 1 else low)
            bit_slot(ack_level)
        # STOP: SCL low, SDA low, SCL high, SDA rises a quarter later
        scl.extend([(2 * q, low), (2 * q, high)])
        sda.extend([(q, sda[-1][1]), (2 * q, low), (q, high)])
        seg = scl if kind == "i2c_scl" else sda
        idle_level = high
    else:
        raise ValueError(f"not a serial signal kind: {kind!r}")
    dur = np.array([d for d, _ in seg], np.float64)
    total = float(dur.sum())
    if total >= period:
        raise ValueError(f"{kind}: the message takes {total * 1e6:.1f} us, longer than period {period * 1e6:.1f} us; "
                         "raise period or shorten text")
    times = np.concatenate([[0.0], np.cumsum(dur)])
    levels = np.array([lv for _, lv in seg] + [idle_level], np.float64)
    out = (times, levels, period)
    _SERIAL_CACHE[key] = out
    return out


def _spi_words(msg: bytes, nbits: int) -> list[int]:
    """The message bytes repacked as ``nbits``-bit words (big endian, zero padded)."""
    if nbits == 8:
        return list(msg)
    total = int.from_bytes(msg, "big")
    nwords = max(1, -(-len(msg) * 8 // nbits))
    total <<= nwords * nbits - len(msg) * 8
    return [(total >> (nbits * (nwords - 1 - i))) & ((1 << nbits) - 1) for i in range(nwords)]


SIGNAL_KINDS = ("zero", "dc", "sine", "square", "decay", "gate", "gen", "dut_in", "dut_out") + SERIAL_KINDS


def parse_signal(spec: str) -> Signal:
    """``kind:key=val,key=val``; values accept SI units (``80us``, ``10kHz``); ``text=`` of the serial kinds is kept
    as a string."""
    spec = spec.strip()
    kind, _, rest = spec.partition(":")
    kind = kind.strip().lower()
    if kind not in SIGNAL_KINDS:
        raise ValueError(f"unknown signal kind {kind!r}; use {', '.join(SIGNAL_KINDS[1:])} or zero")
    params: dict[str, Any] = {}
    for item in filter(None, (s.strip() for s in rest.split(","))):
        k, _, v = item.partition("=")
        k = k.strip().lower()
        params[k] = v.strip() if k == "text" else parse_quantity(v.strip())
    return Signal(kind, params)


def companion_signal(sig: Signal) -> Signal:
    """Default CH2 for a CH1 signal: the TX gate for a decay, a sync square for periodic signals, the data line of a
    bus whose clock is on CH1."""
    if sig.kind == "decay":
        return Signal("gate", {"period": sig.p("period", 1e-3), "width": sig.p("width", 200e-6), "high": 3.3})
    if sig.kind in ("sine", "square"):
        return Signal("square", {"freq": sig.p("freq", 1e3), "amp": 1.5, "offset": 1.5, "duty": 0.5})
    if sig.kind == "spi_clk":
        return Signal("spi_data", dict(sig.params))
    if sig.kind == "i2c_scl":
        return Signal("i2c_sda", dict(sig.params))
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
    #: FPGA 0x16: the edge, or the polarity of the 1.26 type (P.TRIG_POLARITY_NAMES)
    edge: int = 0
    level: int = 128
    mode: int = 0
    pretrigger_samples: int = 1500
    total_samples: int = 3000
    #: FPGA 0x1B in 5 ns units: the trigger hold-off since 0x153C (caps HOLDOFF)
    holdoff: int = 0
    auto_timeout: int = 0  # 0 = firmware default
    #: 1.26 (0x5E / 0x5F / 0x60): type P.TRIG_*, pulse width "longer", N, H (the 0x153E fields stay 0 on 1.26)
    ttype: int = P.TRIG_EDGE
    longer: int = 0
    n: int = 0
    h: int = 0
    level2: int = 0
    range_: int = 0
    both_cores: int = 0

    def trigger2(self) -> P.Trigger2:
        """The GET_TRIGGER 28..51 block."""
        flags = (P.TRIG2_F_LONGER if self.longer else 0) | (P.TRIG2_F_RANGE if self.range_ else 0) | \
            (P.TRIG2_F_BOTH_CORES if self.both_cores else 0)
        return P.Trigger2(self.ttype, self.edge, flags, self.source, self.n, self.h, self.level, self.level2,
                          self.holdoff, 0)

    def capture_trig2(self) -> P.CaptureTrig2:
        """The GET_CAPTURE_INFO 96..111 block."""
        return P.CaptureTrig2(self.ttype, self.trigger2().flags, self.n, self.h, self.holdoff)


@dataclass
class PatternState:
    """The pattern table as SET_PATTERN wrote it (protocol 1.27, FPGA 0x153D, contract 7.2): ``steps`` = (level, mark,
    duration in 1/65536 clock). One fraction accumulator per pattern: step k of a burst starts at clock
    floor(B_k / 65536) with B_k the sum of the earlier steps (cleared at every burst start, carried across the wrap of
    a continuous pattern), so every edge lies within one 5 ns clock of its ideal time."""

    mode: int = P.PAT_OFF
    flags: int = 0
    repeat: int = 1
    gap_clocks: int = 0
    steps: tuple[tuple[int, int, int], ...] = ()

    @property
    def idle(self) -> int:
        return 1 if self.flags & P.PAT_F_IDLE_HIGH else 0

    @property
    def start_source(self) -> int:
        return self.flags >> P.PAT_START_SHIFT & 3

    @property
    def marker_trigger(self) -> bool:
        return bool(self.flags & P.PAT_F_MARKER_TRIGGER)

    @property
    def bounds(self) -> np.ndarray:
        """B_0 = 0 .. B_n = the burst length, in 1/65536 clock."""
        return np.concatenate(([0], np.cumsum([d for _, _, d in self.steps], dtype=np.int64))).astype(np.int64)

    @property
    def burst_clocks(self) -> int:
        return int(self.bounds[-1]) >> 16

    @property
    def bursts(self) -> int:
        return self.repeat if self.mode == P.PAT_REPEAT else 1

    def train_clocks(self) -> int | None:
        """Clocks from a start to the end of its last burst (None: continuous, 0: off)."""
        if self.mode == P.PAT_OFF or not self.steps:
            return 0
        if self.mode == P.PAT_CONTINUOUS:
            return None
        return self.bursts * (self.burst_clocks + self.gap_clocks) - self.gap_clocks

    def _locate(self, c: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        """(valid, step index) at clocks c since a train's start; index n = the gap of a continuous pattern."""
        B = self.bounds
        c = np.asarray(c, np.int64)
        if self.mode == P.PAT_CONTINUOUS:
            T = int(B[-1]) + self.gap_clocks * 65536
            x = (c + 1) * 65536 - 1
            idx = np.searchsorted(B, np.mod(x, T), side="right") - 1
            return c >= 0, idx
        lc = self.burst_clocks
        per = lc + self.gap_clocks
        k = np.floor_divide(c, per)
        w = c - k * per
        valid = (c >= 0) & (k < self.bursts) & (w < lc)
        idx = np.searchsorted(B, (w + 1) * 65536 - 1, side="right") - 1
        return valid, idx

    def levels(self, c: np.ndarray) -> np.ndarray:
        """The pin level (0 / 1) at clocks c since a train's start (idle before it and after it)."""
        c = np.asarray(c, np.int64)
        if self.mode == P.PAT_OFF or not self.steps:
            return np.full(c.shape, self.idle, np.int8)
        valid, idx = self._locate(c)
        lv = np.array([lvl for lvl, _, _ in self.steps] + [self.idle], np.int8)
        return np.where(valid, lv[np.clip(idx, 0, len(self.steps))], self.idle).astype(np.int8)

    def mark_clocks(self, c0: int, c1: int) -> np.ndarray:
        """The first clock of every marked step in [c0, c1) since a train's start (the marker's 1-clock pulse)."""
        if self.mode == P.PAT_OFF or not self.steps or c1 <= max(c0, 0):
            return np.zeros(0, np.int64)
        B = self.bounds
        offs = np.array([int(B[k]) for k, (_, m, _) in enumerate(self.steps) if m], np.int64)
        if not offs.size:
            return np.zeros(0, np.int64)
        c0 = max(c0, 0)
        if self.mode == P.PAT_CONTINUOUS:
            T = int(B[-1]) + self.gap_clocks * 65536
            j0 = max(0, (c0 * 65536) // T - 1)
            j1 = (c1 * 65536) // T + 2
            if j1 - j0 > 1_000_000:
                j1 = j0 + 1_000_000
            js = np.arange(j0, j1, dtype=np.int64)
            out = ((js[:, None] * T + offs[None, :]) >> 16).ravel()
        else:
            per = self.burst_clocks + self.gap_clocks
            ks = np.arange(max(0, c0 // per - 1), min(self.bursts, c1 // per + 2), dtype=np.int64)
            out = (ks[:, None] * per + (offs[None, :] >> 16)).ravel()
        out = np.sort(out)
        return out[(out >= c0) & (out < c1)]

    def state_at(self, c: int) -> tuple[bool, bool, int, int]:
        """(running, in gap, step, bursts done) at clock c since the start."""
        train = self.train_clocks()
        if c < 0 or train == 0:
            return False, False, 0, 0
        if train is not None and c >= train:
            return False, False, 0, self.bursts
        valid, idx = self._locate(np.array([c]))
        n = len(self.steps)
        if self.mode == P.PAT_CONTINUOUS:
            T = int(self.bounds[-1]) + self.gap_clocks * 65536
            return True, int(idx[0]) >= n, int(idx[0]) % max(n, 1), int((c * 65536) // T)
        per = self.burst_clocks + self.gap_clocks
        return True, not bool(valid[0]), int(idx[0]) if valid[0] else 0, int(c // per)


@dataclass
class GenState:
    """The calibration pin's generator (protocol 1.27, FPGA 0x153D): the two-step square of SET_GEN (``p16`` /
    ``h16`` in 1/65536 of a 5 ns clock, source 0) or a SET_PATTERN table (source 1). ``enable`` = 0x50 (the pin is
    driven). ``starts`` = the capture-time starts of the pattern's trains, set per simulated capture."""

    enable: int = 0
    p16: int = P.gen_p16(1_000_000)
    h16: int = P.gen_p16(1_000_000) // 2
    source: int = P.GEN_SOURCE_SQUARE
    pattern: PatternState | None = None
    starts: tuple[float, ...] = ()

    @property
    def frequency_hz(self) -> float:
        return P.GEN_CLOCK_HZ * 65536 / self.p16 if self.source == P.GEN_SOURCE_SQUARE and self.p16 else 0.0

    @property
    def duty(self) -> float:
        return self.h16 / self.p16 if self.p16 else 0.0

    @property
    def pattern_active(self) -> bool:
        return self.source == P.GEN_SOURCE_PATTERN and self.pattern is not None and self.pattern.mode != P.PAT_OFF

    def pin(self, t: np.ndarray) -> np.ndarray:
        """The generator's level (0 / 1) at capture times t, whether or not the pin is driven."""
        t = np.asarray(t, np.float64)
        if self.source == P.GEN_SOURCE_SQUARE:
            f = self.frequency_hz
            return (np.mod(t * f, 1.0) < self.duty).astype(np.int8) if f else np.zeros(t.shape, np.int8)
        pat = self.pattern
        if pat is None:
            return np.zeros(t.shape, np.int8)
        if not self.starts:
            return np.full(t.shape, pat.idle, np.int8)
        st = np.asarray(self.starts, np.float64)
        i = np.searchsorted(st, t, side="right") - 1
        rel = t - st[np.clip(i, 0, None)]
        c = np.floor(rel * P.PAT_CLOCK_HZ).astype(np.int64)
        return np.where(i >= 0, pat.levels(c), pat.idle).astype(np.int8)

    def mark_times(self, ta: float, tb: float) -> np.ndarray:
        """Capture times of the marker pulses in [ta, tb)."""
        pat = self.pattern
        if not self.pattern_active or not self.starts:
            return np.zeros(0)
        out = []
        st = sorted(self.starts)
        for j, s in enumerate(st):
            end = st[j + 1] if j + 1 < len(st) else math.inf
            c0 = int(math.floor((ta - s) * P.PAT_CLOCK_HZ))
            c1 = int(math.ceil((min(tb, end) - s) * P.PAT_CLOCK_HZ)) + 1
            m = s + pat.mark_clocks(c0, c1) / P.PAT_CLOCK_HZ
            out.append(m[(m >= ta) & (m < tb) & (m < end)])
        return np.concatenate(out) if out else np.zeros(0)


@dataclass
class Settings:
    channels: list[ChannelState] = field(default_factory=lambda: [ChannelState(), ChannelState(range=3)])
    divider: int = 19
    tpd_index: int = 24
    sr_index: int = 4
    trigger: TriggerState = field(default_factory=TriggerState)
    gen: GenState = field(default_factory=GenState)
    #: the Filter setting as the firmware keeps it: bits 1:0 K requested, bits 4..2 0, bit 5 P.DECIM_SET_FIR (1.11)
    decim: int = 0
    #: 1.26: the ACQ menu's Sample row = Peak (SET_DECIM filter 4; the Filter word above is kept for the way back)
    peak: int = 0

    def copy(self) -> "Settings":
        return Settings([replace(c) for c in self.channels], self.divider, self.tpd_index, self.sr_index,
                        replace(self.trigger), replace(self.gen), self.decim, self.peak)


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
    #: the only value is "al3" (one bitstream since fw 0.2.102; kept so existing command lines and tests work)
    variant: str = "al3"
    #: the version word the simulated FPGA reports (command 0x06). Anything but FPGA_VERSION models a scope whose
    #: bitstream was not flashed for this firmware: GET_INFO 78 / GET_HEALTH 126 bit 0 set, every command outside
    #: P.BASIC_CMDS answers E_UNSUPPORTED detail 1
    fpga_version: int = FPGA_VERSION
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
    fw_version: str = "0.1.4-sim"
    serial: str = "1013D-SIM"
    #: UI acquisition period (LOCAL in the legacy model, RUN in shared mode); <= 0: no automatic UI captures
    ui_capture_period_s: float = 0.05
    #: legacy exclusive LOCAL/REMOTE firmware (protocol 1.2, no SHARED_MODE)
    legacy: bool = False
    #: protocol minor the shared-mode model reports (None: P.PROTO_MINOR); below P.YIELD_MINOR (23) it models firmware
    #: before 0.2.78, where a request while a menu is open gets E_BUSY instead of closing it
    proto_minor: int | None = None
    #: front-end zero error per channel and core (codes added to the raw code), like the owner's unit (T8: the
    #: cores sit 5 codes apart, the stored compensation corrects 4)
    zero_error: tuple[tuple[float, float], tuple[float, float]] = ((0.0, 0.0), (0.0, 0.0))
    #: true codes per offset-PWM count (None: the firmware formula -100 / dc_shift_value)
    offset_slope: float | None = None
    #: signal on the calibration pin used as EXT trigger input; None: CH2's signal
    ext_signal: Signal | None = None
    #: drop (never send) responses with a larger payload, like firmware 0.1.0 on hardware (> 5 KiB failed)
    fail_responses_over: int | None = None
    #: extra raw zero (codes) per channel and core while the encode clock is gated (legacy divider >= 1), like the
    #: owner's unit (bench 2026-10-18: CH1 core A -0.8, core B -3.6)
    gated_zero_shift: tuple[tuple[float, float], tuple[float, float]] = ((0.0, 0.0), (0.0, 0.0))
    #: relative gain (codes per volt, 1 = the firmware's uv_per_lsb) per channel, clock class (full, gated) and core
    #: (A, B), like the owner's unit (bench 2026-10-18, CH1 range 2: full A 1.020 / B 0.988, gated 1.002 / 0.998)
    core_gain: tuple[tuple[tuple[float, float], tuple[float, float]], ...] = (((1.0, 1.0), (1.0, 1.0)),
                                                                              ((1.0, 1.0), (1.0, 1.0)))
    #: protocol 1.12: a bound calibration v3 block measured from this model (GET_CAL version 3, corrected traces)
    cal_v3: bool = False
    #: protocol 1.15 (with cal_v3): the full-rate class drifted after the calibration, per channel (d, e, q, cm) in
    #: corrected codes: core A reads + (d + e x + q x^2) / 2 + cm, core B - (d + e x + q x^2) / 2 + cm (x = code - 128);
    #: the firmware's tracker is modelled as converged (it reports and takes out d, e and cm; q is reported only)
    v4: tuple[tuple[float, float, float, float], tuple[float, float, float, float]] | None = None
    #: input window (volts at the BNC, +-) per hardware range 0..5 of the stage in front of the offset injection: a
    #: level beyond it pins the stage, and the noise (which the model puts in front of it) goes with the signal (bench
    #: 2026-10-19: -1.2 V DC saturates 0.1 and 0.2 V/div, the ADC then reads a noise-free constant); None: no limit
    frontend_window_v: tuple[float, float, float, float, float, float] | None = None
    #: protocol 1.20 READ_BODE reason 8 (fw 0.2.71 / 0.2.83): the device sweep ends in an ADC readout fault after its
    #: pre-scan (the self-check flagged a core on 0x153A+, or the pre-scan clipped at 5 V/div on older bitstreams)
    bode_readout_fault: bool = False
    #: protocol 1.27 logger: the DDR ring's size (the firmware's is about 8 MB) and a stall of the host's reads
    #: (LOG_READ answers nothing for this many seconds from 0.1 s after LOG_START while the log keeps producing: with
    #: a small ring the DDR ring overflows and the next chunk carries the gap)
    log_ring_bytes: int = 8 << 20
    log_stall_s: float = 0.0


#: the simulated running build's commit time (yy mm dd hh mm ss, UTC) and hash (GET_INFO fw_build)
SIM_BUILD_TIME = (26, 10, 19, 1, 2, 3)
SIM_BUILD = 0x51A0001


def make_egon_image(size: int, kind: bytes = b"eGON.EXE", fill: int = 0, version: str | None = None,
                    ident: P.ImageIdentity | None = None) -> bytes:
    """A minimal eGON image: branch, magic, checksum and 512-aligned length (mksunxi.py rule), then a pattern.
    ``version``: embed the firmware's ``#VER`` literal (what fnirsi1013d-flash --persist compares). ``ident``: the
    fw 0.2.52 identity record linked last (the image ends with it, then zero padding to 512)."""
    raw = size
    size = (size + 511) // 512 * 512
    buf = bytearray(((fill + i * 7) & 0xFF for i in range(size)))
    buf[0:4] = b"\x06\x00\x00\xea"
    buf[4:12] = kind
    if ident is not None:
        end = (raw + 3) // 4 * 4
        if end + 4 > size:
            end = size - 4
        buf[end - 36: end] = P.pack_ident_record(ident)
        buf[end:] = bytes(size - end)
    if version is not None:
        lit = f"#OK,FNIRSI-1013D-AI,fw={version},proto=1.3,fpga=0x".encode() + b"\0"
        buf[64: 64 + len(lit)] = lit
    struct.pack_into("<I", buf, 16, size)
    struct.pack_into("<I", buf, 12, P.egon_checksum(bytes(buf), size))
    return bytes(buf)


class SimCard:
    """SD card behind the SD sector commands (fw 0.2.2): sparse 512-byte sectors, an MBR whose first partition
    starts at ``part_start``, the Atlan bootloader at 16 and a scope image at 750. Test hooks: ``present``,
    ``fail_next_write`` (read-back mismatch), ``io_error_next`` (driver error, e.g. -2 timeout). ``limit``: the
    firmware's window cap (PC_SD_LIMIT: 16384 from fw 0.2.101, 2048 up to 0.2.100)."""

    def __init__(self, card_sectors: int = 131072, part_start: int = 8192, limit: int = P.SD_WINDOW_LIMIT):
        self.card_sectors = card_sectors
        self.limit = limit
        self.sectors: dict[int, bytes] = {}
        self.present = True
        self.fail_next_write = 0
        self.io_error_next = 0
        self.writes: list[tuple[int, int]] = []
        mbr = bytearray(512)
        if part_start:
            struct.pack_into("<BBBBBBBBII", mbr, 446, 0, 0, 0, 0, 0x0C, 0, 0, 0, part_start,
                             card_sectors - part_start)
            mbr[510:512] = b"\x55\xaa"
        self.put(0, bytes(mbr))
        self.put(16, make_egon_image(16 * 1024, b"eGON.BT0", 0x30))
        self.put(80, make_egon_image(8 * 1024, b"eGON.EXE", 0x50))
        self.put(750, make_egon_image(300 * 1024, b"eGON.EXE", 0x75))

    def put(self, sector: int, data: bytes) -> None:
        for i in range(0, len(data), 512):
            self.sectors[sector + i // 512] = bytes(data[i: i + 512]).ljust(512, b"\0")

    def read(self, sector: int, count: int) -> bytes:
        return b"".join(self.sectors.get(s, bytes(512)) for s in range(sector, sector + count))

    @property
    def part_start(self) -> int:
        mbr = self.read(0, 1)
        if mbr[510:512] != b"\x55\xaa":
            return 0
        return struct.unpack_from("<I", mbr, 446 + 8)[0]

    @property
    def write_end(self) -> int:
        ps = self.part_start
        return min(ps, self.limit) if ps else 0

    def header(self, sector: int, verify: bool) -> P.SdHeaderInfo:
        head = self.read(sector, 1)
        magic, length, stored = P.egon_info(head)
        h = P.SdHeaderInfo(sector, magic, P.SD_CHECK_NOT, length if magic else 0, stored if magic else 0)
        if magic and verify:
            if length == 0 or length % 4 or sector + (length + 511) // 512 > self.limit:
                h.check = P.SD_CHECK_LENGTH
            else:
                body = self.read(sector, (length + 511) // 512)
                h.check = P.SD_CHECK_OK if P.egon_checksum(body, length) == stored else P.SD_CHECK_BAD
        return h


class TrigQual:
    """The 0x153C trigger qualifier (docs/plans/fpga-0x153c.md 3.3), a Python port of ``tb_trigtypes.v``'s reference
    model: fed the trigger stream chunk by chunk (8-bit codes, 16-bit filter outputs or the EXT level as 0 / 255), it
    returns the index of the trigger sample, which is the edge trigger's for the qualifying edge (the first sample on
    the far side of the level), or the qualifying sample for the timeout type.

    Pulse width: a pulse of w samples on the pulse side (polarity 0 high) that began after the arm fires on its
    trailing edge when w < N (shorter) or w > N (longer). Nth edge: the Nth edge in the counted direction since the arm;
    with H > 0 the count restarts after more than H samples without any edge. Timeout: the sample that makes a run of
    the held level (polarity 0 high) N + 1 samples long, counted from the edge into it or from the arm."""

    def __init__(self, tr: "TriggerState", level: int):
        self.type = tr.ttype
        self.pol = tr.edge
        self.longer = bool(tr.longer)
        self.n = max(1, (tr.n & 0xFFFF) if tr.ttype == P.TRIG_NTH_EDGE else tr.n)
        self.h = tr.h
        self.level = level
        self.pos = 0  # absolute index of the next sample fed
        self.side: bool | None = None  # the previous sample at or above the level
        self.pulse_start: int | None = None  # pulse width: where the current pulse began (None: off side / before the arm)
        self.rem = self.n  # Nth edge: edges still to count
        self.last_edge: int | None = None  # Nth edge: the last edge of either direction (or the arm)
        self.run_start: int | None = None  # timeout: where the current run of the held level began

    def feed(self, a: np.ndarray, prev: int | None, pos: int) -> int:
        """``a`` = samples pos .. pos + len(a) - 1 of the stream, ``prev`` the sample before (None at the arm).
        Returns the relative index of the trigger sample in ``a`` or -1."""
        s = np.asarray(a).astype(np.int64) >= self.level
        if self.side is None:  # the arm
            self.pos = pos
            self.side = bool(prev >= self.level) if prev is not None else bool(s[0])
            self.last_edge = pos
            held = self.pol == 0
            if self.type == P.TRIG_TIMEOUT and self.side == held:
                self.run_start = pos
        sp = np.concatenate(([self.side], s[:-1]))
        edges = np.flatnonzero(s != sp)
        out = -1
        if self.type == P.TRIG_PULSE_WIDTH:
            out = self._pulse(s, edges)
        elif self.type == P.TRIG_NTH_EDGE:
            out = self._nth(s, edges)
        elif self.type == P.TRIG_TIMEOUT:
            out = self._timeout(s, edges)
        self.side = bool(s[-1])
        self.pos += len(s)
        return out

    def _pulse(self, s: np.ndarray, edges: np.ndarray) -> int:
        pulse_side = self.pol == 0
        for e in edges:
            e = int(e)
            if bool(s[e]) == pulse_side:
                self.pulse_start = self.pos + e
            elif self.pulse_start is not None:
                w = self.pos + e - self.pulse_start
                self.pulse_start = None
                if (w > self.n) if self.longer else (w < self.n):
                    return e
        return -1

    def _nth(self, s: np.ndarray, edges: np.ndarray) -> int:
        counted = self.pol == 0
        for e in edges:
            e = int(e)
            ea = self.pos + e
            if self.h and self.last_edge is not None and ea - self.last_edge > self.h:
                self.rem = self.n
            self.last_edge = ea
            if bool(s[e]) == counted:
                self.rem -= 1
                if self.rem == 0:
                    self.rem = self.n
                    return e
        return -1

    def _timeout(self, s: np.ndarray, edges: np.ndarray) -> int:
        held = self.pol == 0
        for e in edges:
            e = int(e)
            if self.run_start is not None and self.run_start + self.n < self.pos + e:
                return self.run_start + self.n - self.pos
            self.run_start = self.pos + e if bool(s[e]) == held else None
        if self.run_start is not None and self.run_start + self.n <= self.pos + len(s) - 1:
            return self.run_start + self.n - self.pos
        return -1


def signal_edges(sig: Signal, gen: "GenState", high: float, low: float
                 ) -> tuple[float, list[float], list[float]] | None:
    """Schmitt edges of a periodic signal within one period: (period, rising times, falling times), the comparator
    going high at >= ``high`` volts and low below ``low``. None for a signal without a period (dc, zero) or without
    edges at these levels. Analytic for sine / square / gate / gen / the serial kinds, a COUNTER_GRID walk for the
    others (decay, dut_*)."""
    k = sig.kind
    if k in ("zero", "dc"):
        return None
    if k == "sine":
        f, off, amp = sig.p("freq", 1e3), sig.p("offset", 0.0), sig.p("amp", 0.3)
        if amp <= 0 or f <= 0:
            return None
        x1, x2 = (high - off) / amp, (low - off) / amp
        if not (-1 < x1 < 1) or not (-1 < x2 < 1):
            return None
        period, ph = 1.0 / f, sig.p("phase", 0.0)
        th_r = math.asin(x1)  # rising crossing of ``high`` (cos > 0 there)
        th_f = math.pi - math.asin(x2)  # falling crossing of ``low``
        return period, [((th_r - ph) / (2 * math.pi)) % 1.0 * period], [((th_f - ph) / (2 * math.pi)) % 1.0 * period]
    if k in ("square", "gate", "gen"):
        if k == "square":
            f, off, amp = sig.p("freq", 1e3), sig.p("offset", 0.0), sig.p("amp", 0.3)
            hi_v, lo_v, duty = off + amp, off - amp, sig.p("duty", 0.5)
            period = 1.0 / f if f > 0 else 0.0
        elif k == "gate":
            hi_v, lo_v = sig.p("high", 3.3), sig.p("low", 0.0)
            period, duty = sig.p("period", 1e-3), sig.p("width", 200e-6) / sig.p("period", 1e-3)
        else:
            if not gen.enable or gen.frequency_hz <= 0:
                return None
            hi_v, lo_v, period, duty = sig.p("high", 3.3), 0.0, 1.0 / gen.frequency_hz, gen.duty
        if period <= 0 or not (hi_v >= high and lo_v < low) or not 0 < duty < 1:
            return None
        return period, [0.0], [duty * period]
    if k in SERIAL_KINDS:
        times, levels, period = serial_timeline(k, sig.params)
        start = sig.p("start", 0.0)
        r, fl = [], []
        state = bool(levels[-1] >= high)
        for t, v in zip(times, levels):
            if not state and v >= high:
                r.append((float(t) + start) % period)
                state = True
            elif state and v < low:
                fl.append((float(t) + start) % period)
                state = False
        return (period, sorted(r), sorted(fl)) if r and fl else None
    if k in ("decay", "dut_in", "dut_out"):
        if k == "decay":
            period = sig.p("period", 1e-3)
        else:
            if not gen.enable or gen.frequency_hz <= 0:
                return None
            period = 1.0 / gen.frequency_hz
        t = np.arange(COUNTER_GRID) * (period / COUNTER_GRID)
        v = sig.eval(t, gen)
        r, fl = [], []
        state = bool(v[-1] >= high)
        for i in range(COUNTER_GRID):
            x = float(v[i])
            if not state and x >= high:
                r.append(float(t[i]))
                state = True
            elif state and x < low:
                fl.append(float(t[i]))
                state = False
        return (period, r, fl) if r and fl else None
    return None


def peak_mid_codes(mn: np.ndarray, mx: np.ndarray, gain_a: int, gain_b: int, off_a: int, off_b: int) -> np.ndarray:
    """pc_peak_mid (fw-c6): the processed trace sample of a peak pair, through the mean of the two cores' Q16
    correction of the full clock class (gain = round((gain_a + gain_b) / 2), off = (off_a + off_b) / 2 truncated):
    clamp(((min + max) gain + 2 off + 2^16) >> 17)."""
    gain = (int(gain_a) + int(gain_b) + 1) // 2
    s = int(off_a) + int(off_b)
    off = s // 2 if s >= 0 else -((-s) // 2)
    v = ((np.asarray(mn, np.int64) + np.asarray(mx, np.int64)) * gain + 2 * off + 65536) >> 17
    return np.clip(v, 0, 255).astype(np.uint8)


def gate_count(period: float, rising: list[float], falling: list[float], t_start: float, gate: float
               ) -> tuple[int, int, int]:
    """The counter's E, T, W (5 ns clocks) for the rising edges of a periodic signal inside [t_start, t_start + gate]:
    E periods from the first rising edge to the last, T clocks between them, W high clocks in them (the pulse that
    starts at the last edge lies after it)."""
    widths = []
    for r in rising:
        nxt = [f for f in falling if f > r]
        widths.append((nxt[0] if nxt else falling[0] + period) - r)
    first = math.inf
    last = -math.inf
    count = 0
    w_sum = 0.0
    w_last = 0.0
    for r, w in zip(rising, widths):
        m0 = math.ceil((t_start - r) / period - 1e-12)
        m1 = math.floor((t_start + gate - r) / period + 1e-12)
        if m1 < m0:
            continue
        n = m1 - m0 + 1
        count += n
        w_sum += n * w
        if r + m0 * period < first:
            first = r + m0 * period
        if r + m1 * period > last:
            last, w_last = r + m1 * period, w
    if count < 2:
        return 0, 0, 0
    clk = 1.0 / P.COUNTER_CLOCK_HZ
    return count - 1, int(round((last - first) / clk)), int(round((w_sum - w_last) / clk))


class Deferred:
    def __init__(self, frame: P.Frame, done_at: float, result: Callable[[], tuple[int, bytes]]):
        self.frame = frame
        self.done_at = done_at
        self.result = result


class Device:
    """The protocol state machine of the scope firmware plus a model of the FPGA."""

    def __init__(self, cfg: SimConfig | None = None, clock: Callable[[], float] = time.monotonic):
        self.cfg = cfg or SimConfig()
        if self.cfg.variant not in VARIANT_NAMES:
            raise ValueError(f"variant {self.cfg.variant!r}: the firmware supports one bitstream, use 'al3' and "
                             f"fpga_version for the word the FPGA reports")
        self.clock = clock
        self.rng = np.random.default_rng(self.cfg.seed)
        self.pyrng = random.Random(self.cfg.seed)
        self.t_boot = clock()
        self.settings = Settings()
        self.settings.trigger.pretrigger_samples = 1500
        self.settings.trigger.total_samples = 3000
        self.cal = [Calibration(adc_b_comp=-round(self.cfg.core_b_offset[0])),
                    Calibration(adc_b_comp=-round(self.cfg.core_b_offset[1]))]
        self.snapshot: Settings | None = None
        self.control = P.Control.LOCAL
        #: a modal screen is open on the scope (GET_STATUS ui_busy; 2 = STARTING). Protocol 1.23: a request that needs the
        #: main loop closes it (every modal screen by its cancel path) unless ui_hold names a step that must finish
        self.ui_busy = False
        #: 1.23: P.BusyDetail.CAL / SAVE / USB_DRIVE while such a step runs (E_BUSY with it), 0 none
        self.ui_hold = 0
        self.ui_yields = 0  # requests that closed the modal screens (firmware GET_LINK_STATS 68)
        self.ui_yield_refused = 0  # E_BUSY answers instead (69)
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
        R = RING_WORDS
        self.ring = self.rng.integers(100, 156, size=(4, R), dtype=np.int64).astype(np.uint8)
        self._pending_capture: dict | None = None
        self.capture_divider = self.settings.divider
        #: ring words of the capture: pretrigger words written, total words
        self.capture_geometry = (750, 1500)
        #: the capture's samples (pretrigger, total) and decimation (k = 0: legacy 8-bit)
        self.capture_samples = (1500, 3000)
        self.capture_decim = P.DecimConfig()
        #: decimation written at the last arm (GET_DECIM bytes 24/25, FPGA 0x29)
        self.decim_written: P.DecimConfig | None = None
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
        #: protocol 1.1 main-loop timing fields (simulated as a steady 5 ms loop)
        self.pass_ms = 5
        self.pass_max_ms = 5
        #: GET_LINK_STATS counters (protocol 1.2); the ones the simulator can observe are counted
        self.link: dict[str, int] = {n: 0 for n in P.LINK_STATS_NAMES}
        # --- collaborative mode (protocol 1.3)
        self.shared = bool(self.caps & P.Caps.SHARED_MODE)
        if self.shared:
            self.control = P.Control.SHARED
        self.run_state = P.RunState.RUN if self.shared else P.RunState.STOP
        self.settings_seq = 0
        self.capture_settings_seq = 0
        self.capture_origin = int(P.CaptureOrigin.UI_RUN)
        self.capture_settings = self.settings.copy()
        self.ui_t0 = clock()
        self.ui_done = -1  # the first poll in RUN produces a capture
        #: the ring holds capture ``capture_id`` (False after an ARM until the next capture finishes)
        self.ring_valid = False
        #: test hook: called as on_arm(device) at the start of every host ARM (e.g. a UI change mid-run)
        self.on_arm: Callable[["Device"], None] | None = None
        #: test hook: the next n host captures are one saturated value on every core (the FPGA 0x153A first record
        #: after a rate / filter / range change, bench 2026-10-20)
        self.flat_next = 0
        #: GET_EVENT_LOG ring (fw 0.2.12)
        self.events: list[P.DeviceEvent] = []
        self.event_total = 0
        self.ev_frozen = False
        self.fpga_trace = False  # 1.22: event type 22 level (GET_EVENT_LOG flags bits 2:1), off after boot
        self.ev_reset_ms = 0
        self.ev_snapshot: list[P.DeviceEvent] = []
        #: fw 0.2.24 bus-level ring (P.BUS_EVENT_TYPES only, 16 entries)
        self.bus_events: list[P.DeviceEvent] = []
        self.bus_total = 0
        #: fw 0.2.28 spectrum view (ui_set_spectrum): display mode, window (0 rect, 1 Hann, 2 flat-top), the view's
        #: FFT length (log2, capped by the record) and channel selection (0 CH1, 1 CH2, 2 both)
        self.display_mode = P.DisplayMode.SCOPE
        #: protocol 1.20: the Bode view and sweep engine; ``_internal`` is set while the engine's own requests run
        self.bode = SimBode(self)
        self._internal = False
        self.spec_window = 1
        self.spec_log2n = 14
        self.spec_channel = 2
        self.spec_frames = 0
        self._spec_key: tuple | None = None
        self._spec: dict[int, tuple[P.SpectrumHeader, np.ndarray]] = {}
        #: SD card and the 64 KiB write staging buffer (fw 0.2.2)
        self.sd = SimCard()
        self.sd_stage = bytearray(P.SD_STAGE_BYTES)
        #: protocol 1.16 (row 55): unit life (sector 709 words 224..255), the stored restart record (a
        #: parse_life_record 'restart' dict or None), the calibration provenance (P.Provenance or None; prov_state 2
        #: with ``cal_prov_bad``), the card image identity cache and the reports SAVE_REPORT wrote (name -> text)
        self.power_on = 12
        self.minutes_stored = 2200
        self.life_flags = 0x0B  # record found, boot write ok, sector valid
        self.last_end = 1
        self.settings_changed = False
        self.restart_record: dict | None = None
        self.restart_record_this_power_on = False
        self.cal_prov: P.Provenance | None = None
        self.cal_prov_bad = False
        self.ident_cache: P.SdIdent | None = None
        self.reports: dict[str, str] = {}
        #: protocol 1.17: the acquisition menu's Average (test hook; GET_STATUS 46 / 47), the host sequencer setting
        #: (a SetSeqReq; mode 0 = plain captures) and the latest capture's run (a dict from simulate_run, or None)
        self.ui_average = False
        self.seq_setting = P.SetSeqReq()
        self.run: dict | None = None
        #: the FPGA still holds that run (no arm since)
        self.run_held = False
        #: protocol 1.19 full-rate encode check (GET_HEALTH 88..103): counters and the last 0x33 byte; test hooks
        #: ``enc_bad_next`` (the next n full-rate captures are found garbled and re-taken clean after ``enc_rearms``
        #: re-arms) and ``enc_unrecovered_next`` (the next n stay garbled after 16 re-arms and are accepted, flagged)
        self.enc_checked = self.enc_bad = self.enc_recovered = 0
        self.enc_last33 = 0
        self.enc_flags = 0
        self.enc_bad_next = 0
        self.enc_unrecovered_next = 0
        self.enc_rearms = 1
        #: protocol 1.24 capture stages (0x153A, GET_HEALTH 104..123, GET_CAL 356..363): the 0x63 value in effect,
        #: the stored trailer (None, or (value, fpga version word) as trained), the last 0x36 counts and the
        #: counters; test hook ``cap_counts_next`` (a list of 4-count tuples the next full-rate captures read; the
        #: model's captures are clean, 0 outliers, otherwise)
        self.cap_stored: tuple[int, int] | None = None
        self.cap_value = 0
        self.cap_flags = 0
        self.cap_counts: tuple[int, ...] = (0, 0, 0, 0)
        self.cap_flagged = 0
        self.cap_reads = self.cap_flagged_n = 0
        self.cap_counts_next: list[tuple[int, int, int, int]] = []
        #: protocol 1.26: the trigger hold-off (0x1B) still to run at the next arm, from the latest capture's trigger
        #: (across MCU re-arms, as the FPGA's counter; cleared by 0x66 + ARM) and the counter's configuration (the
        #: firmware's boot values: gate 100 ms, the trigger source, level 0 = follow the trigger level, hysteresis 6)
        #: and last read sequence
        self.holdoff_left_s = 0.0
        #: 0x66 = 1 written (boot, SET_TRIGGER hold-off, SET_TRIGGER2): the next 0x01 = 1 clears a running hold-off
        self.holdoff_clear_pending = True
        #: GET_STATUS 48..51 (0x37, 0x34 byte 0, edges still to count): zeroed at ARM, read while a capture is in flight
        self.trig_status: tuple[int, int, int] = (0, 0, 0)
        self.counter_gate = 2
        self.counter_source = 0
        self.counter_level = P.COUNTER_LEVEL_FOLLOW  # as configured; counter_level_now() is the one in effect
        self.counter_hyst = P.COUNTER_DEFAULT_HYST
        self.counter_seq_read: int | None = None
        #: protocol 1.27 (FPGA 0x153D): the pattern's MCU start (device time; None = not started), the commit not yet
        #: taken (device time it is taken), the pattern trigger engine (a P.Trigger2, armed, re-arm, event seen)
        self.pat_start_dev: float | None = None
        self.pat_pending_until = 0.0
        self.pat_marker_seen = False
        self.ptrig = P.Trigger2(level=128)
        self.ptrig_armed = 0
        self.ptrig_rearm = 0
        self.ptrig_event = False
        #: the streaming logger (dict while a log exists since the last LOG_START; see _cmd_log_start) and the test
        #: hooks: ``log_lap_next`` (the next chunk with unread words follows an FPGA lap of up to that many)
        self.logger: dict | None = None
        self.log_lap_next = 0
        self.cap_boot()

    # --- capabilities -------------------------------------------------------------------------------

    @property
    def caps(self) -> int:
        if self.cfg.caps_override is not None:
            return self.cfg.caps_override
        # The firmware's fixed word for FPGA_VERSION (pc_variant_init) plus what only the simulator implements
        # (ACQUIRE_AVG, DEBUG_FPGA, FORCE; the averaging model is 8-bit, ACQUIRE_AVG refuses decimated records)
        c = P.Caps.SCREENSHOT | P.Caps.DEVICE_AVERAGE | P.Caps.DEBUG_RAW | P.Caps.ENTER_FEL
        c |= P.Caps.READ_LAG_UNVERIFIED | P.Caps.FORCE_TRIGGER
        c |= P.Caps.PROG_TRIGGER_POS | P.Caps.GENERATOR | P.Caps.EXT_TRIGGER | P.Caps.DECIM | P.Caps.FIR
        c |= P.Caps.SEQ | P.Caps.ENC_CHECK
        if self.cfg.legacy:  # the 1.2 model: before fw 0.2.72 read 0x33 at boot, the core order was unverified
            c |= P.Caps.INTERLEAVE_UNVERIFIED
        else:
            c |= P.Caps.SHARED_MODE | P.Caps.SD_SECTORS
        if self.cap_on:
            c |= P.Caps.CAPTURE_STAGES
        if self.trig26_on:  # 1.26 (FPGA 0x153C): trigger types, the hold-off, peak detect, the counter
            c |= P.Caps.TRIG_TYPES | P.Caps.HOLDOFF | P.Caps.PEAK_DETECT | P.Caps.COUNTER
        if self.d27_on:  # 1.27 (FPGA 0x153D): the logger, the pattern generator and its trigger engine
            c |= P.Caps.LOGGER | P.Caps.PATTERN_GEN | P.Caps.PATTERN_TRIGGER
        return int(c) & ~self.cfg.caps_clear

    @property
    def trig26_on(self) -> bool:
        """Protocol 1.26 (FPGA 0x153C): the firmware drives the trigger qualifier, the hold-off, peak detect and the
        counter (caps bits 2, 20-22, features bits 8-10). From the config, as ``cap_on``."""
        return not self.cfg.legacy and (self.cfg.proto_minor is None or self.cfg.proto_minor >= 26)

    @property
    def d27_on(self) -> bool:
        """Protocol 1.27 (FPGA 0x153D): the logger, the pattern generator, its trigger engine (caps 23, 24, 27,
        features 11-13)."""
        return not self.cfg.legacy and (self.cfg.proto_minor is None or self.cfg.proto_minor >= 27)

    @property
    def mismatch(self) -> bool:
        """Protocol 1.25: the FPGA reported another version word than the firmware is built for (GET_INFO 78 bit 0)."""
        return self.cfg.fpga_version != FPGA_VERSION

    @property
    def fpga_flags(self) -> int:
        return P.FPGA_F_MISMATCH if self.mismatch else 0

    #: GET_HEALTH 88: the FPGA's 0x33 self-check (the built-for bitstream; mode 1 would need a boot 0x33 with an unknown
    #: convention, which the model does not produce)
    enc_mode = 2

    @property
    def features(self) -> int:
        """GET_STATUS 44-45 (protocol 1.17): from the bitstream, not the caps."""
        return int(P.Features.EXT_TRIGGER | P.Features.DECIM | P.Features.FIR | P.Features.SEGMENTS |
                   P.Features.HW_AVERAGE | P.Features.ENC_CHECK | P.Features.BODE |
                   (P.Features.CAPTURE_STAGES if self.cap_on else 0) |
                   (P.Features.TRIG_TYPES | P.Features.PEAK | P.Features.COUNTER if self.trig26_on else 0) |
                   (P.Features.LOGGER | P.Features.PATTERN | P.Features.PATTERN_TRIGGER if self.d27_on else 0))

    def ui_average_state(self) -> tuple[int, int]:
        """GET_STATUS 46 / 47 (protocol 1.17, one behaviour since fw 0.2.102): the acquisition menu's Average
        (``ui_average`` test hook): 0 off; else the mean of N records, 2 by the sequencer (a legacy record that fits
        the accumulator), 1 by the MCU (16-bit records, deeper legacy records), log2 N 0 when two records do not fit
        the 40 ms budget (single records). Off in single trigger mode (the UI averages RUN records only)."""
        if not self.ui_average or self.settings.trigger.mode == 2:
            return 0, 0
        if self.decim_config().peak:  # 1.26: averaging of peak words is never armed (the sums would mix max and min)
            return 0, 0
        Pw, Tw, _ = self.geometry()
        return P.ui_average_plan(Tw, Pw, self.settings.divider, bool(self.decim_config().k))

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
            zero_code_q8=(P.cal_target(cs.dcoffset, cal.dc_shift_value) * 256 if self.cfg.cal_v3
                          else P.zero_code_q8(cs.dcoffset, cal.dc_shift_value)))

    # --- signal generation ---------------------------------------------------------------------------

    def _analog(self, ch: int, t: np.ndarray, core: str | None, settings: Settings) -> np.ndarray:
        """Noise-free core code (float) at times t; ``core`` None = the mean of both cores."""
        sig = self.cfg.signals[ch]
        v = sig.eval(t, settings.gen)
        cs = settings.channels[ch]
        if cs.coupling == 1:
            v = v - sig.dc
        if self.cfg.frontend_window_v is not None:
            w = self.cfg.frontend_window_v[min(cs.range, 5)]
            v = np.clip(v, -w, w)
        cal = self.cal[ch]
        vpl = P.uv_per_lsb_q8(cal.input_calibration[cs.range], cs.range) / 256.0 * 1e-6
        if self.cfg.offset_slope is None:
            zero = P.zero_code_q8(cs.dcoffset, cal.dc_shift_value) / 256.0
        else:
            zero = 128.0 + self.cfg.offset_slope * cs.dcoffset
        g = self.cfg.core_gain[ch]
        if core is None:  # filtered: the ADCs run at full rate, both cores weigh equally
            out = zero + v / vpl * (g[0][0] + g[0][1]) / 2 + \
                (self.raw_zero_offset(ch, 0, 0) + self.raw_zero_offset(ch, 1, 0)) / 2
            if self.cfg.v4 is not None:  # the A - B parts cancel in the mean of the cores; cm stays
                g_ref = (g[1][0] + g[1][1]) / 2
                out = out + self.cfg.v4[ch][3] * (g[0][0] + g[0][1]) / 2 / g_ref
            return out
        k, cls = (0 if core == "A" else 1), self._clock_class(settings)
        out = zero + v / vpl * g[cls][k] + self.raw_zero_offset(ch, k, cls)
        if self.cfg.v4 is not None and cls == P.CAL_CLASS_FULL:
            d, e, q, cm = self.cfg.v4[ch]
            g_ref = (g[1][0] + g[1][1]) / 2
            x = (P.cal_target(cs.dcoffset, cal.dc_shift_value) - 128) + v / vpl * g_ref
            s = 1 if k == 0 else -1
            out = out + (s * (d + e * x + q * x * x) / 2 + cm) * g[0][k] / g_ref  # corrected codes -> raw codes
        return out

    def raw_zero_offset(self, ch: int, core: int, cls: int) -> float:
        """Model: the raw 0 V code of a core minus the offset formula's zero, for clock class cls (1 = gated)."""
        off = self.cfg.zero_error[ch][core] + (self.cfg.core_b_offset[ch] if core == 1 else 0.0)
        return off + (self.cfg.gated_zero_shift[ch][core] if cls == P.CAL_CLASS_GATED else 0.0)

    def _clock_class(self, settings: Settings) -> int:
        return P.cal_clock_class(settings.divider, self.decim_config(settings).k > 0)

    def _offset_zero(self, ch: int, dcoffset: int) -> float:
        """The offset formula's 0 V code (what the true slope gives at dcoffset)."""
        if self.cfg.offset_slope is None:
            return P.zero_code_q8(dcoffset, self.cal[ch].dc_shift_value) / 256.0
        return 128.0 + self.cfg.offset_slope * dcoffset

    def _v3_tables(self, ch: int) -> P.CalResp:
        """The stored v3 block a base calibration of this model measures (range-independent here): zeros and slopes
        per class and core, gains G_k[c] = g_ref / g_k[c] (Q15) with g_ref the gated mean of both cores."""
        c = self.cal[ch]
        slope = self.cfg.offset_slope if self.cfg.offset_slope is not None else -100.0 / c.dc_shift_value
        zero = []
        for k in (0, 1):
            for core in (0, 1):
                zero += [int(round(16 * (self._offset_zero(ch, 0) + self.raw_zero_offset(ch, core, k))))] * 6
        g = self.cfg.core_gain[ch]
        g_ref = (g[1][0] + g[1][1]) / 2
        gains = tuple(int(round(32768 * g_ref / g[k][core])) for k in (0, 1) for core in (0, 1))
        return P.CalResp(ch, pwm=tuple(c.dc_calibration_offset[:6]), zero=tuple(zero),
                         slope=tuple([int(round(65536 * slope))] * 24),
                         noise=tuple([int(round(16 * self.cfg.noise_lsb))] * 24), gain=gains, gain_range=2,
                         gain_flags=0, gain_lever=1416)

    def _v3_corr(self, ch: int, settings: Settings) -> dict:
        """adc_cal.c acal_correction for the channel's settings: per-core Q16 gain / offset of the 8-bit readout
        (class by the legacy divider), comp / frac at 0 V, the 16-bit path gain16 / corr16 (full-class zeros)."""
        cs = settings.channels[ch]
        d, r = cs.dcoffset, min(cs.range, 5)
        cls = P.CAL_CLASS_GATED if settings.divider else P.CAL_CLASS_FULL
        tb = self._v3_tables(ch)
        t16 = 16 * P.cal_target(d, self.cal[ch].dc_shift_value)
        out: dict = {"gain": [], "off": [], "comp": [], "frac": [], "t16": t16}
        zf = []
        for k in (0, 1):
            zc16 = tb.zero16_at(cls, k, r, d)
            zf.append(tb.zero16_at(P.CAL_CLASS_FULL, k, r, d))
            g = tb.gain_q16(cls, k)
            out["gain"].append(g)
            out["off"].append(P._rdiv(t16 * 65536 - g * zc16, 16))
            d16 = max(-127 * 16, min(127 * 16, t16 - zc16))
            out["comp"].append(d16 // 16)
            out["frac"].append(d16 - 16 * (d16 // 16))
        out["gain16"] = tb.gain16_full()
        out["corr16"] = P._rdiv(t16 * 131072 - out["gain16"] * (zf[0] + zf[1]), 131072)
        if self.cfg.v4 is not None:  # 1.15: the tracker folded into the readouts (row 54)
            d, e, _q, cm = self.cfg.v4[ch]
            dq, eq, cmq = round(d * 256) / 256, round(e * 2 ** 24) / 2 ** 24, round(cm * 256) / 256
            out["corr16"] -= round(16 * cmq)
            if cls == P.CAL_CLASS_FULL:  # c' = c (1 - s e / 2) - s (d - 128 e) / 2 - cm
                for k, s in ((0, 1), (1, -1)):
                    f = 1 - s * eq / 2
                    out["gain"][k] = round(out["gain"][k] * f)
                    out["off"][k] = round(out["off"][k] * f - 65536 * (s * (dq - 128 * eq) / 2 + cmq))
        return out

    def _peak_corr(self, ch: int, settings: Settings) -> tuple[int, int, int, int]:
        """fpga_channel_correction_full (fw-c6): the 8-bit readout's per-core Q16 gain / offset of the full clock class
        (the correction at divider 0, v4 tracker included) whatever the divider: the ADCs run at 200 MSa/s under peak
        detect, while GET_CAL 92..107 report the divider's (gated) class."""
        if not self.cfg.cal_v3:
            c = self.cal[ch]
            return 65536, 65536, c.adc_a_comp * 65536, c.adc_b_comp * 65536
        v = self._v3_corr(ch, replace(settings, divider=0))
        return v["gain"][0], v["gain"][1], v["off"][0], v["off"][1]

    def _corr16(self, ch: int, settings: Settings) -> tuple[int, int]:
        """16-bit path (gain16 Q16, corr16 in 1/16 code) the firmware applies: v3, else 1 and 8 x (comp_a + comp_b)."""
        if not self.cfg.cal_v3:
            return 65536, 8 * (self.cal[ch].adc_a_comp + self.cal[ch].adc_b_comp)
        c = self._v3_corr(ch, settings)
        return c["gain16"], c["corr16"]

    def _pinned(self, ch: int, t: np.ndarray, settings: Settings) -> np.ndarray | None:
        """Where the front-end window (``frontend_window_v``) pins the stage (None: no window model)."""
        w = self.cfg.frontend_window_v
        if w is None:
            return None
        sig = self.cfg.signals[ch]
        cs = settings.channels[ch]
        v = sig.eval(t, settings.gen) - (sig.dc if cs.coupling == 1 else 0.0)
        return np.abs(v) >= w[min(cs.range, 5)]

    def _codes(self, ch: int, t: np.ndarray, core: str, settings: Settings,
               noise: np.ndarray | None = None) -> np.ndarray:
        """Uncompensated 8-bit codes of one core at times t. ``noise`` (LSB) defaults to fresh noise."""
        code = self._analog(ch, t, core, settings)
        if noise is None and self.cfg.noise_lsb > 0:
            noise = self.rng.normal(0.0, self.cfg.noise_lsb, size=code.shape)
        if noise is not None:
            pin = self._pinned(ch, t, settings)
            code = code + (noise if pin is None else np.where(pin, 0.0, noise))
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

    def _ext_words(self, t0: float, k0: int, n: int, dt: float, settings: Settings,
                   dec: P.DecimConfig | None = None) -> np.ndarray:
        """EXT input per write clock as codes 0 / 255: the pin's logic level, seen EXT_LATENCY_S late."""
        sig = self.cfg.ext_signal or self.cfg.signals[1]
        k = np.arange(k0, k0 + n, dtype=np.float64)
        if dec is not None and dec.k:
            t = t0 + k * dec.n * 5e-9 - EXT_LATENCY_S  # one output sample per word
        else:
            # Legacy layout: the AD9288 outputs a sample 4 encode cycles (words) after taking it, so the EXT edge is
            # seen that many words before the data show it (modelled as the firmware's legacy EXT shift, 1.22)
            lag_words = P.ext_shift_legacy(settings.divider, True) // 2
            t = t0 + 2 * (k + lag_words) * dt + dt - EXT_LATENCY_S
        return np.where(sig.eval(t, settings.gen) > EXT_THRESHOLD_V, 255, 0).astype(np.uint8)

    def _word_times(self, t0: float, k: np.ndarray, dt: float, settings: Settings,
                    dec: P.DecimConfig | None) -> np.ndarray:
        """The time each ring word's EXT / marker sample stands for (the EXT apply path, without the pin's latency)."""
        if dec is not None and dec.k:
            return t0 + k * dec.n * 5e-9
        lag_words = P.ext_shift_legacy(settings.divider, True) // 2
        return t0 + 2 * (k + lag_words) * dt + dt

    def _marker_words(self, t0: float, k0: int, n: int, dt: float, settings: Settings,
                      dec: P.DecimConfig | None = None) -> np.ndarray:
        """1.27 trigger source 3: 255 on the word whose write clock takes a marker pulse (the first clock of a marked
        step, through the EXT apply path), else 0; nothing unless the pattern runs with its marker as a trigger source
        (SET_PATTERN PAT_F_MARKER_TRIGGER)."""
        out = np.zeros(n, np.uint8)
        g = settings.gen
        if not g.pattern_active or g.pattern is None or not g.pattern.marker_trigger:
            return out
        k = np.arange(k0 - 1, k0 + n, dtype=np.float64)
        tw = self._word_times(t0, k, dt, settings, dec)
        m = g.mark_times(float(tw[0]), float(tw[-1]))
        if m.size:
            idx = np.searchsorted(tw, m + 5e-9, side="left") - 1  # the word after the clock that carries the pulse
            idx = idx[(idx >= 0) & (idx < n)]
            out[idx] = 255
        return out

    def decim_config(self, settings: Settings | None = None) -> P.DecimConfig:
        """The decimation an arm with these settings writes (pc_decim_next)."""
        s = settings or self.settings
        has = P.DECIM_HAS_FIR
        if self.cap_on:
            has |= P.DECIM_HAS_CAPSTAGES
        k_req = P.DECIM_PEAK if s.peak and self.trig26_on else s.decim & 3
        return P.decim_config(k_req, s.decim & P.DECIM_SET_FIR, s.divider,
                              s.trigger.total_samples, has)

    def _peak_samples(self, ch: int, t0: float, k0: int, n: int, settings: Settings, seed: int,
                      dec: P.DecimConfig) -> tuple[np.ndarray, np.ndarray]:
        """1.26 peak detect: (min, max) raw codes of words k0..k0+n-1 of one channel. Word k covers the N raw sample
        instants (both cores, 5 ns apart) ending DECIM_LATENCY_S (+ the 0x153A capture latency) before its write at
        t0 + k N 5 ns; the noise-free core-mean code at up to PEAK_SUBSAMPLES of them (all for N <= that, a uniform
        grid above) plus per-instant noise (seeded per block of words, as the filter model), rounded and clipped, then
        the window's extremes. Narrow spikes are caught as the FPGA catches them (every raw sample looked at)."""
        N = dec.n
        Q = min(N, PEAK_SUBSAMPLES)
        step = N * 5e-9 / Q
        lat = DECIM_LATENCY_S + (CAP_EXTRA_LATENCY_S if dec.cap else 0.0)
        mn = np.empty(n, dtype=np.uint8)
        mx = np.empty(n, dtype=np.uint8)
        B = 256
        sigma = math.sqrt(self.cfg.noise_lsb ** 2) if self.cfg.noise_lsb > 0 else 0.0
        for b0 in range(k0 // B, (k0 + n - 1) // B + 1):
            lo_k = max(k0, b0 * B)
            hi_k = min(k0 + n, (b0 + 1) * B)
            k = np.arange(lo_k, hi_k, dtype=np.float64)
            t_end = t0 + k * N * 5e-9 - lat
            t = (t_end[:, None] - (np.arange(Q)[None, ::-1] + 0.5) * step).ravel()
            c = self._analog(ch, t, None, settings)
            if sigma > 0:
                nz = np.random.default_rng((seed, b0 + (3 << 40), ch)).normal(0.0, sigma, (B, Q))
                c = c + nz[lo_k - b0 * B: hi_k - b0 * B].ravel()
                pin = self._pinned(ch, t, settings)
                if pin is not None:
                    c = np.where(pin, self._analog(ch, t, None, settings), c)
            codes = np.clip(np.rint(c), 0, 255).reshape(-1, Q)
            mn[lo_k - k0: hi_k - k0] = codes.min(axis=1)
            mx[lo_k - k0: hi_k - k0] = codes.max(axis=1)
        return mn, mx

    def _decim_samples(self, ch: int, t0: float, k0: int, n: int, settings: Settings, seed: int,
                       dec: P.DecimConfig) -> np.ndarray:
        """Stored 16-bit samples k0..k0+n-1 of one channel with the filter on (boxcar / CIC2 / CIC3, 1.11 FIR).

        Word k is written at t0 + k N 5 ns; its newest raw sample was taken DECIM_LATENCY_S (FIR_LATENCY_S) earlier.
        The CIC is the K-fold boxcar of C raw samples (C = N, or N / 2 with the FIR active, which then filters the
        CIC output with the 15 RTL taps and keeps every second sample), modelled on Q = min(C, 64) sub-samples per
        CIC period of the noise-free core-mean code (clipped to the 8-bit rails); the raw noise (cfg.noise_lsb plus
        1/12 LSB^2 of quantisation) enters at the output with the kernel's exact variance. Stored = min(65535,
        floor(y / 2^S)), y = mean x gain. The noise is seeded per block of words, so the trigger search and the
        stored ring agree."""
        N, K = dec.n, dec.k
        C = N // 2 if dec.fir else N
        Q = min(C, DECIM_SUBSAMPLES)
        w = analysis.decim_kernel(K, Q, dec.fir)
        L = w.size
        per = 2 * Q if dec.fir else Q  # grid points per output sample
        step = C * 5e-9 / Q
        t_end0 = t0 + k0 * N * 5e-9 - (FIR_LATENCY_S if dec.fir else DECIM_LATENCY_S) - CAP_EXTRA_LATENCY_S
        g = t_end0 - (L - 1) * step + np.arange((n - 1) * per + L) * step
        c = np.clip(self._analog(ch, g, None, settings), 0.0, 255.0)
        mean = np.convolve(c, w, "valid")[::per][:n]
        pin = self._pinned(ch, g, settings)
        live = None if pin is None else 1.0 - np.convolve(pin.astype(np.float64), w / w.sum(), "valid")[::per][:n]
        if self.cfg.noise_lsb > 0:
            B = self._NOISE_BLOCK
            b0, b1 = k0 // B, (k0 + n - 1) // B
            nz = np.concatenate([np.random.default_rng((seed, b + (2 << 40), ch)).normal(0.0, 1.0, B)
                                 for b in range(b0, b1 + 1)])[k0 - b0 * B: k0 - b0 * B + n]
            sigma = math.sqrt(self.cfg.noise_lsb ** 2 + 1 / 12) * math.sqrt(float(np.sum(w * w)) * Q / C)
            mean = mean + sigma * nz * (1.0 if live is None else live)
        y = np.clip(mean, 0.0, 255.0) * dec.gain
        return np.minimum(np.floor(y / (1 << dec.shift)), 65535).astype(np.uint16)

    def _dwords(self, t0: float, k0: int, n: int, settings: Settings, seed: int, dec: P.DecimConfig) -> np.ndarray:
        """(4, n) ring bytes of a decimated capture: per channel low byte at core A, high byte at core B (1.26 peak
        records: the window's minimum at core A, its maximum at core B)."""
        out = np.empty((4, n), dtype=np.uint8)
        for ch in (0, 1):
            if dec.peak:
                out[2 * ch], out[2 * ch + 1] = self._peak_samples(ch, t0, k0, n, settings, seed, dec)
                continue
            v = self._decim_samples(ch, t0, k0, n, settings, seed, dec)
            out[2 * ch] = v & 0xFF
            out[2 * ch + 1] = v >> 8
        return out

    def _trig_stream(self, tr: TriggerState, dec: P.DecimConfig, wide: bool, ext: bool, t0: float, dt: float,
                     settings: Settings, seed: int) -> tuple[Callable[[int, int], np.ndarray], TriggerState]:
        """The samples the trigger looks at (one per ring word) and the trigger state with the level in their units:
        EXT = the pin level as 0 / 255 (level 128); filtered = the 16-bit output of the source channel with the 16-bit
        level; peak (1.26) = the window maximum for a rising detection, the minimum for a falling one (the firmware
        writes level << 8); legacy = core A of the source channel."""
        src = tr.source
        if src == P.TRIG_SOURCE_PATTERN:
            tr = replace(tr, level=128)
            return (lambda k0, n: self._marker_words(t0, k0, n, dt, settings, dec)), tr
        if ext:
            tr = replace(tr, level=128)
            return (lambda k0, n: self._ext_words(t0, k0, n, dt, settings, dec)), tr
        if wide and dec.peak:
            # the detect direction: the polarity, except for pulse width where the trigger is the trailing edge
            rising = (tr.edge == 0) != (tr.ttype == P.TRIG_PULSE_WIDTH)

            def peak_stream(k0: int, n: int) -> np.ndarray:
                mn, mx = self._peak_samples(src, t0, k0, n, settings, seed, dec)
                return mx if rising else mn
            return peak_stream, tr
        if wide:
            tr = replace(tr, level=P.decim_level16(tr.level, dec.gain, dec.shift))
            return (lambda k0, n: self._decim_samples(src, t0, k0, n, settings, seed, dec)), tr

        def legacy_stream(k0: int, n: int) -> np.ndarray:
            k = np.arange(k0, k0 + n, dtype=np.float64)
            ta = t0 + 2 * k * dt + (dt if self.cfg.true_order == "b_first" else 0.0)
            nz = self._noise(seed, k0, n)
            return self._codes(src, ta, "A", settings, None if nz is None else nz[2 * src])
        return legacy_stream, tr

    def _search_trigger(self, stream: Callable[[int, int], np.ndarray], k_from: int, limit: int, tr: TriggerState,
                        hold_from: int = 0) -> int:
        """Word index of the trigger sample at or after max(k_from, hold_from) and before k_from + limit, or -1: the
        edge compare (``_find_trigger``) or the 1.26 qualifier (``TrigQual``), both seeing the sample before the start
        as ``prev``. ``hold_from`` (1.26): the trigger hold-off masks the compare until that word."""
        start = max(k_from, hold_from)
        if start >= k_from + limit:
            return -1
        prev = int(stream(start - 1, 1)[0])
        qual = TrigQual(tr, tr.level) if tr.ttype in (P.TRIG_PULSE_WIDTH, P.TRIG_NTH_EDGE, P.TRIG_TIMEOUT) else None
        k = start
        step = 2048  # most captures trigger within a period: start small, then widen
        while k < k_from + limit:
            n = min(step, k_from + limit - k)
            a = stream(k, n)
            idx = qual.feed(a, prev, k) if qual is not None else self._find_trigger(a, prev, tr)
            if idx >= 0:
                return k + idx
            prev = int(a[-1])
            k += n
            step = min(2 * step, 65536)
        return -1

    #: the pattern trigger engine's events searched per capture (the model looks at a window, the FPGA for ever)
    ENGINE_MAX_EVENTS = 256
    ENGINE_MAX_WORDS = 1 << 17

    def _pattern_starts(self, settings: Settings, dec: P.DecimConfig, t0: float, dt: float, wp: float, seed: int,
                        n_words: int, dev_t0: float) -> tuple[float, ...]:
        """1.27: the capture-time starts of the pattern's trains: the MCU's start (SET_PATTERN start = 1, placed by
        device time) and, with start source 'trigger' / 'both' and the engine armed, every event of the pattern
        trigger engine on its own stream (the TrigQual model, the channel signals with the pin not driven), the
        burst starting one write clock + 20 ns after a CH1 / CH2 event and 20 ns after an EXT one; a start during a
        train is ignored, one shot takes the first event."""
        pat = settings.gen.pattern
        assert pat is not None
        starts: list[float] = []
        src = pat.start_source
        if self.pat_start_dev is not None and src in (P.PAT_START_MCU, P.PAT_START_BOTH):
            starts.append(t0 + (self.pat_start_dev - dev_t0))
        if src in (P.PAT_START_TRIGGER, P.PAT_START_BOTH) and self.ptrig_armed:
            quiet = settings.copy()
            quiet.gen.enable = 0
            pt = self.ptrig
            tr = TriggerState(source=pt.source, edge=pt.polarity, level=pt.level, mode=1, ttype=pt.type,
                              longer=int(bool(pt.flags & P.TRIG2_F_LONGER)), n=pt.n, h=pt.h)
            ext = pt.source == P.TRIG_SOURCE_EXT
            stream, tr = self._trig_stream(tr, dec, dec.k > 0, ext, t0, dt, quiet, seed)
            train = pat.train_clocks()
            lat = 20e-9 if ext else wp + 20e-9
            k = 1
            end = min(n_words, self.ENGINE_MAX_WORDS)
            while k < end and len(starts) < self.ENGINE_MAX_EVENTS:
                ke = self._search_trigger(stream, k, end - k, tr)
                if ke < 0:
                    break
                self.ptrig_event = True
                s = t0 + ke * wp + lat
                starts.append(s)
                if not self.ptrig_rearm or train is None:
                    break
                k = ke + 1 + math.ceil(train * 5e-9 / wp)
        starts.sort()
        kept: list[float] = []
        train = pat.train_clocks()
        for s in starts:
            if kept and (train is None or s < kept[-1] + train * 5e-9):
                continue
            kept.append(s)
        return tuple(kept)

    def holdoff_words(self, tr: TriggerState, wp: float) -> int:
        """The trigger hold-off (0x1B, 5 ns units) in ring words of period ``wp``, rounded up."""
        return math.ceil(tr.holdoff * 5e-9 / wp - 1e-9) if tr.holdoff else 0

    def _find_trigger(self, a: np.ndarray, prev: int | None, trig: TriggerState) -> int:
        """Index in a of the first crossing (current sample) or -1. prev: sample before a[0]."""
        if prev is None:
            if len(a) < 2:
                return -1
            pa, ca, off = a[:-1].astype(np.int32), a[1:].astype(np.int32), 1
        else:
            pa = np.concatenate(([prev], a[:-1])).astype(np.int32)
            ca, off = a.astype(np.int32), 0
        lvl = trig.level
        if trig.edge == 0:
            hit = (pa < lvl) & (ca >= lvl)
        else:
            hit = (pa >= lvl) & (ca < lvl)
        idx = np.flatnonzero(hit)
        return int(idx[0]) + off if len(idx) else -1

    def geometry(self, settings: Settings | None = None) -> tuple[int, int, int]:
        """pretrigger_words, total_words, auto_timeout write clocks. 1.9 decimated: one sample per word, and with
        the EXT source the pretrigger words are lowered by the EXT latency shift (row 47)."""
        s = settings or self.settings
        tr = s.trigger
        dec = self.decim_config(s)
        shift = self.ext_shift_for(dec, s.divider, tr.source, tr.pretrigger_samples)
        if dec.k:
            return tr.pretrigger_samples - shift, tr.total_samples, tr.auto_timeout or tr.total_samples
        return (tr.pretrigger_samples - shift) // 2, tr.total_samples // 2, tr.auto_timeout or tr.total_samples

    def ext_shift_for(self, dec: P.DecimConfig, divider: int, source: int, pre: int) -> int:
        """The EXT shift the firmware applies (pc_ext_shift): the filter latency with K >= 1; 1.22 (fw 0.2.72) also
        the ADC pipeline in the legacy layout. At most the pretrigger, whole words in the legacy layout."""
        if source not in (2, P.TRIG_SOURCE_PATTERN):  # 1.27: the marker takes the EXT apply path
            return 0
        if not dec.k and self.proto_minor < 22:
            return 0
        v = dec.ext_shift_at(divider)
        return v if v <= pre else (pre if dec.k else pre & ~1)

    def simulate_capture(self, settings: Settings, forced: bool = False, dev_t0: float | None = None) -> dict:
        """Run one capture through the FPGA model. Returns a dict with the new ring, trigger address, the
        modelled durations and flags. Normal mode without a trigger returns triggered=False, never_done.
        ``dev_t0``: the device time of the capture's word 0 (default now; the pattern's starts are placed on the
        capture's timeline from it)."""
        R = RING_WORDS
        Pw, Tw, auto_to = self.geometry(settings)
        dt = self.dt(settings.divider)
        dec = self.decim_config(settings)
        wp = dec.n * 5e-9 if dec.k else 2 * dt  # write period of one ring word
        t0 = self.pyrng.uniform(0.0, 10.0)
        w0 = self.pyrng.randrange(R)
        seed = self.pyrng.randrange(1 << 62)
        settings = settings.copy()
        if settings.gen.pattern_active:
            limit0 = (auto_to if settings.trigger.mode == 0 else (1 << 21)) + Pw + Tw
            settings.gen.starts = self._pattern_starts(settings, dec, t0, dt, wp, seed, limit0,
                                                       self.now() if dev_t0 is None else dev_t0)
            if settings.gen.starts and settings.gen.pattern is not None and settings.gen.pattern.mark_clocks(
                    0, (settings.gen.pattern.train_clocks() or (1 << 40))).size:
                self.pat_marker_seen = True
        tr = settings.trigger
        ext = tr.source == 2
        trig_samples, tr = self._trig_stream(tr, dec, dec.k > 0, ext, t0, dt, settings, seed)

        def make_words(k0: int, n: int) -> np.ndarray:
            if dec.k:
                return self._dwords(t0, k0, n, settings, seed, dec)
            return self._words(t0, k0, n, dt, settings, seed)
        kt = -1
        auto = tr.mode == 0
        limit = auto_to if auto else (1 << 21)
        # 1.26: a hold-off still running from the previous capture's trigger masks the compare (the model counts it
        # from the arm, as if the re-arm followed the DONE at once)
        hold_from = math.ceil(self.holdoff_left_s / wp - 1e-9) if self.holdoff_left_s > 0 else 0
        if not forced:
            kt = self._search_trigger(trig_samples, Pw, limit, tr, hold_from)
        res: dict = {"t0": t0, "dt": dt, "divider": settings.divider, "Pw": Pw, "Tw": Tw,
                     "settings": settings.copy(), "seq": self.settings_seq, "decim": dec, "wp": wp,
                     "samples": (settings.trigger.pretrigger_samples, settings.trigger.total_samples)}
        # did the pin toggle during the capture window (GET_CAPTURE_INFO bit 5)?
        e = self._ext_words(t0, Pw, Tw + Pw, dt, settings, dec)
        res["gen_starts"] = settings.gen.starts
        res["ext_edge"] = bool(np.any(np.diff(e.astype(np.int16)) != 0))
        if kt >= 0:
            end = kt + (Tw - Pw)
            k_first = end - R
            words = make_words(k_first, R)
            addr = (w0 + np.arange(k_first, end)) % R
            ring = self.ring.copy()
            ring[:, addr] = words
            trig_addr = (w0 + kt + self.cfg.trigger_align_offset) % R
            res.update(ring=ring, trigger_addr=trig_addr, triggered=True, auto_timeout=False,
                       t_trigger=kt * wp, t_done=end * wp)
            return res
        if auto or forced:
            # auto timeout: FPGA forces trigger_addr = pretrigger_words; data contiguous, not synchronised
            end = Pw + (auto_to if not forced else 0) + Tw
            words = make_words(end - R, R)
            ring = np.empty_like(self.ring)
            # stream word (end - Tw + a) sits at address a for a in [0, Tw); older words wrap behind
            addr = (np.arange(end - R, end) - (end - Tw)) % R
            ring[:, addr] = words
            res.update(ring=ring, trigger_addr=Pw % R, triggered=False, auto_timeout=True,
                       t_trigger=None, t_done=(Pw + (auto_to if not forced else 0) + Tw) * wp)
            return res
        res.update(ring=None, triggered=False, auto_timeout=False, t_trigger=None, t_done=None)
        return res

    # --- sequencer (protocol 1.17, FPGA 0x1536) -------------------------------------------------------------

    #: exact simulation of at most this many averaged records (8-bit / 16-bit records; a 16-bit record costs a filter
    #: model); longer runs scale the sums of these, so the simulated noise of the mean stops falling there
    SEQ_SIM_MAX_RECORDS = 128
    SEQ_SIM_MAX_RECORDS_WIDE = 16

    def _seq_fit(self, sq: P.SetSeqReq, settings: Settings | None = None) -> int:
        Pw, Tw, _ = self.geometry(settings)
        return P.seq_fit(sq.mode, sq.flags, sq.m, sq.count, sq.n, Pw, Tw, self.decim_config(settings).k > 0)

    def _segment_run_latest(self) -> bool:
        return self.run is not None and self.run["mode"] == P.SEQ_SEGMENTS

    def simulate_run(self, settings: Settings, sq: P.SetSeqReq) -> dict:
        """A sequencer run on one continuous signal timeline (word 0 = the release of the FPGA reset). Returns a
        pending-capture dict: ``finish(elapsed_s, stopped)`` builds the ring and the run for the records done by then
        (``t_done`` None when the run never ends by itself: circular, or normal mode without triggers)."""
        Pw, Tw, auto_to = self.geometry(settings)
        dt = self.dt(settings.divider)
        dec = self.decim_config(settings)
        wide = dec.k > 0
        wp = dec.n * 5e-9 if wide else 2 * dt
        t0 = self.pyrng.uniform(0.0, 10.0)
        seed = self.pyrng.randrange(1 << 62)
        tr = settings.trigger
        ext = tr.source == 2
        trig_samples, tr = self._trig_stream(tr, dec, wide, ext, t0, dt, settings, seed)

        def make_words(k0: int, n: int) -> np.ndarray:
            return self._dwords(t0, k0, n, settings, seed, dec) if wide else self._words(t0, k0, n, dt, settings, seed)

        auto = tr.mode == 0
        limit = auto_to if auto else 1 << 21
        # 1.26: the trigger hold-off (0x1B) runs from each record's trigger across the sequencer's restarts
        thold_w = self.holdoff_words(tr, wp)
        hold0 = math.ceil(self.holdoff_left_s / wp - 1e-9) if self.holdoff_left_s > 0 else 0

        def find(k_from: int, hold_from: int = 0) -> int:
            return self._search_trigger(trig_samples, k_from, limit, tr, max(hold_from, hold0))

        hold_w = math.ceil(P.seq_holdoff_units(sq.holdoff_ns) * 5e-9 / wp - 1e-9)
        base = {"divider": settings.divider, "Pw": Pw, "Tw": Tw, "settings": settings.copy(), "seq": self.settings_seq,
                "decim": dec, "samples": (settings.trigger.pretrigger_samples, settings.trigger.total_samples),
                "auto_timeout": False, "trigger_addr": Pw % RING_WORDS, "wp": wp, "thold_w": thold_w}
        if sq.mode == P.SEQ_SEGMENTS:
            return self._simulate_segments(sq, base, make_words, find, auto, auto_to, Pw, Tw, wp, hold_w, wide)
        return self._simulate_average(sq, base, make_words, find, auto, auto_to, Pw, Tw, wp, hold_w, wide)

    def _simulate_segments(self, sq, base, make_words, find, auto, auto_to, Pw, Tw, wp, hold_w, wide) -> dict:
        m, circular = sq.m, bool(sq.flags & P.SEQ_F_CIRCULAR)
        slot_w = 1 << m
        slots = P.seq_slots(sq.mode, sq.flags, m, sq.count)
        target = slots + slots // 2 + 1 if circular else sq.count  # a circular run wraps once in the model
        segs: list[tuple[int, int, bool, float]] = []  # slot fill start word, trigger word, triggered, end time
        k = 0
        ends = True
        thold_w = base.get("thold_w", 0)
        hold_from = 0
        for _ in range(target):
            kt = find(k + Pw, hold_from)
            trig = kt >= 0
            if not trig:
                if not auto:
                    ends = False
                    break
                kt = k + Pw + auto_to
            else:
                hold_from = kt + thold_w  # 1.26: no trigger accepted until the hold-off has elapsed
            end = kt - Pw + Tw + P.SEQ_TAIL
            segs.append((k, kt, trig, end * wp))
            k = end + hold_w
        ends = ends and not circular

        def finish(elapsed: float, stopped: bool) -> dict:
            done = [s for s in segs if s[3] <= elapsed]
            ring = self.ring.copy()
            entries = [0] * slots
            for i, (k0, kt, trig, _end) in enumerate(done):
                s = i % slots
                off = (kt - k0) % slot_w
                words = make_words(kt - Pw, Tw)
                pos = s * slot_w + (off - Pw + np.arange(Tw)) % slot_w
                ring[:, pos] = words
                entries[s] = P.SeqEntry(int(round(kt * wp / 5e-9)), off, trig, True).encode()
            n = len(done)
            fl = 0
            if circular:
                fl |= P.SEQ_RUN_CIRCULAR
                entries[n % slots] &= ~(1 << 63)  # the slot being refilled
                if n >= slots:
                    fl |= P.SEQ_RUN_WRAPPED
            if stopped:
                fl |= P.SEQ_RUN_STOPPED
            run = {"mode": P.SEQ_SEGMENTS, "run_flags": fl, "m": m, "wide": wide, "count": n,
                   "untriggered": min(0xFFFF, sum(1 for s in done if not s[2])), "requested": sq.count, "n": 0,
                   "holdoff_ns": sq.holdoff_ns, "entries": entries, "slots": slots, "Pw": Pw, "Tw": Tw}
            return {**base, "ring": ring, "run": run, "triggered": any(s[2] for s in done)}

        out = {**base, "finish": finish, "run": True, "ring": None, "triggered": False,
               "t_trigger": segs[0][1] * wp if segs and segs[0][2] else None,
               "t_done": segs[-1][3] if ends and segs else None}
        if ends:
            out.update(finish(math.inf, False))
        return out

    def _simulate_average(self, sq, base, make_words, find, auto, auto_to, Pw, Tw, wp, hold_w, wide) -> dict:
        npos, n = sq.count, sq.n
        total = npos * n
        cap = self.SEQ_SIM_MAX_RECORDS_WIDE if wide else self.SEQ_SIM_MAX_RECORDS
        exact = min(total, max(npos, cap // npos * npos))
        dead_w = math.ceil(P.seq_dead_ns(Tw, Pw, int(round(wp * 1e9))) * 1e-9 / wp - 1e-9)
        recs: list[tuple[np.ndarray, float]] = []  # per accumulated record: lanes (cores or 16-bit channels), end time
        untrig: list[float] = []
        k = 0
        attempts = 0
        while len(recs) < exact and attempts < 4 * exact + 16:
            attempts += 1
            kt = find(k + Pw)
            if kt < 0:
                if not auto:
                    break
                k = k + Pw + auto_to  # an untriggered record is skipped
                untrig.append(k * wp)
                continue
            w = make_words(kt - Pw, Tw).astype(np.int64)
            if wide:
                w = np.stack([w[0] | (w[1] << 8), w[2] | (w[3] << 8)])
            end = kt - Pw + Tw
            recs.append((w, end * wp))
            k = end + dead_w + hold_w
        ends = len(recs) >= exact
        period = recs[-1][1] / len(recs) if recs else 0.0

        def finish(elapsed: float, stopped: bool) -> dict:
            if elapsed == math.inf and ends:
                r = total
            else:
                r = sum(1 for x in recs if x[1] <= elapsed)
                if r == len(recs) and ends and period > 0:
                    r = min(total, max(r, int(elapsed / period)))
            lanes = 2 if wide else 4
            sums = np.zeros((lanes, npos, Tw), np.int64)
            use = min(r, len(recs))
            for i in range(use):
                sums[:, i % npos] += recs[i][0]
            if r > use:  # beyond the exact records: scale per position
                for p in range(npos):
                    have = use // npos + (1 if p < use % npos else 0)
                    want = r // npos + (1 if p < r % npos else 0)
                    if have:
                        sums[:, p] = np.rint(sums[:, p] * (want / have)).astype(np.int64)
            ring = self.ring.copy()
            if use:
                last = recs[use - 1][0]
                if wide:
                    for ch in (0, 1):
                        ring[2 * ch, :Tw] = last[ch] & 0xFF
                        ring[2 * ch + 1, :Tw] = last[ch] >> 8
                else:
                    ring[:, :Tw] = last
            run = {"mode": P.SEQ_AVERAGE, "run_flags": P.SEQ_RUN_STOPPED if stopped else 0, "m": 0, "wide": wide,
                   "count": r, "untriggered": min(0xFFFF, sum(1 for x in untrig if x <= elapsed)), "requested": npos,
                   "n": n, "holdoff_ns": sq.holdoff_ns, "sums": sums, "slots": 0, "Pw": Pw, "Tw": Tw}
            return {**base, "ring": ring, "run": run, "triggered": r > 0}

        out = {**base, "finish": finish, "run": True, "ring": None, "triggered": False,
               "t_trigger": recs[0][1] if recs else None,
               "t_done": (recs[-1][1] if exact == total else period * total) if ends else None}
        if ends:
            out.update(finish(math.inf, False))
        return out

    def _stop_run(self, pc: dict) -> dict:
        """STOP during a run: the records done by now (model time), flag stopped."""
        scale = self.cfg.time_scale
        elapsed = (self.now() - self.arm_time) / scale if scale > 0 else math.inf
        if pc.get("t_done") is not None and elapsed >= pc["t_done"]:
            return pc
        return pc["finish"](elapsed, True)

    def seq_state(self) -> P.SeqState:
        sq = self.seq_setting
        Pw, Tw, _ = self.geometry()
        wide = self.decim_config().k > 0
        r = self.run
        st = cnt = unt = 0
        pc = self._pending_capture
        if pc is not None and "run" in pc and self.acq_state in (P.AcqState.ARMED, P.AcqState.TRIGGERED):
            st = P.SEQ_ST_RUNNING | (sq.mode & 3) | (P.SEQ_ST_FMT16 if wide else 0)
        elif r is not None and self.run_held:
            st = P.SEQ_ST_DONE | r["mode"] | (P.SEQ_ST_FMT16 if r["wide"] else 0) | \
                (P.SEQ_ST_CIRCULAR if r["run_flags"] & P.SEQ_RUN_CIRCULAR else 0) | \
                (P.SEQ_ST_OVERFLOW if r["run_flags"] & P.SEQ_RUN_WRAPPED else 0)
            cnt, unt = r["count"], r["untriggered"]
        return P.SeqState(sq.mode, sq.flags, sq.m, sq.count, sq.n_raw, sq.holdoff_ns,
                          P.seq_holdoff_units(sq.holdoff_ns), self._seq_fit(sq), int(wide), Pw, Tw,
                          P.seq_max_count(sq.mode, sq.m, Tw), st, 0, 0, cnt & 0xFFFFFF, unt, int(self.run_held),
                          self.capture_id if r is not None else 0, r["count"] if r else 0,
                          r["untriggered"] if r else 0, r["mode"] if r else 0, r["run_flags"] if r else 0)

    def _cmd_get_seq(self, req: P.Frame) -> P.Frame:
        return self._ok(req, self.seq_state().pack())

    def _cmd_set_seq(self, req: P.Frame) -> P.Frame:
        r = P.SetSeqReq.unpack(req.payload)
        fit = self._seq_fit(r)
        bad = {P.SEQ_FIT_MODE: 0, P.SEQ_FIT_FLAGS: 1, P.SEQ_FIT_M: 2, P.SEQ_FIT_COUNT: 4, P.SEQ_FIT_N: 6}
        if fit in bad:
            raise _DevErr(P.Status.E_BAD_ARG, bad[fit], P.SEQ_FIT_NAMES[fit])
        if fit in (P.SEQ_FIT_TOTAL, P.SEQ_FIT_PRE):
            raise _DevErr(P.Status.E_STATE, fit, P.SEQ_FIT_NAMES[fit] + ": SET_TRIGGER first")
        if r.mode == P.SEQ_AVERAGE and self.decim_config().peak:  # 1.26: the sums would mix max and min
            raise _DevErr(P.Status.E_STATE, P.SEQ_FIT_PEAK, P.SEQ_PEAK_TEXT)
        self.seq_setting = r if r.mode != P.SEQ_OFF else P.SetSeqReq()
        return self._ok(req, self.seq_state().pack())

    def _held_run(self, mode: int) -> dict:
        r = self.run
        if r is None:
            raise _DevErr(P.Status.E_STATE, 0, "the latest capture is not a run")
        if not self.run_held:
            raise _DevErr(P.Status.E_STATE, 1, "armed since: the run is gone")
        if r["mode"] != mode:
            raise _DevErr(P.Status.E_STATE, 2, "the run is of the other mode")
        return r

    def _cmd_read_seq(self, req: P.Frame) -> P.Frame:
        q = P.ReadSeqReq.unpack(req.payload)
        if q.what > 1:
            raise _DevErr(P.Status.E_BAD_ARG, 0, "what 0 table, 1 averaged record")
        if q.what == P.SEQ_READ_TABLE:
            r = self._held_run(P.SEQ_SEGMENTS)
            if q.first >= r["slots"]:
                raise _DevErr(P.Status.E_BAD_ARG, 4, f"first {q.first} >= {r['slots']} slots")
            if q.count < 1 or q.first + q.count > r["slots"]:
                raise _DevErr(P.Status.E_BAD_ARG, 6, "count")
            body = struct.pack(f"<{q.count}Q", *r["entries"][q.first: q.first + q.count])
            hdr = P.ReadSeqHeader(self.capture_id, q.first, q.count, 0, P.SEQ_FMT_WIDE if r["wide"] else 0, r["m"], 0,
                                  r["Pw"], r["Tw"])
        else:
            if not 1 <= q.ch_mask <= 3:
                raise _DevErr(P.Status.E_BAD_ARG, 1, "ch_mask")
            if q.shift > 31 and q.shift != P.SEQ_SHIFT_SUMS:
                raise _DevErr(P.Status.E_BAD_ARG, 2, "S 0..31 or 0xFF")
            r = self._held_run(P.SEQ_AVERAGE)
            words = r["requested"] * r["Tw"]
            if q.first >= words:
                raise _DevErr(P.Status.E_BAD_ARG, 4, f"first {q.first} >= {words} words")
            if q.count < 1 or q.first + q.count > words:
                raise _DevErr(P.Status.E_BAD_ARG, 6, "count")
            sums = r["sums"].reshape(r["sums"].shape[0], -1)[:, q.first: q.first + q.count]
            blocks = []
            if r["wide"]:
                u32 = q.shift == P.SEQ_SHIFT_SUMS
                fmt = P.SEQ_FMT_WIDE | (P.SEQ_FMT_U32 if u32 else 0)
                applied = q.shift
                for ch in (0, 1):
                    if q.ch_mask & (1 << ch):
                        v = sums[ch]
                        blocks.append(v.astype("<u4").tobytes() if u32 else
                                      np.minimum(65535, v >> q.shift).astype("<u2").tobytes())
            else:
                fmt = 0
                applied = 0 if q.shift == P.SEQ_SHIFT_SUMS else q.shift
                for ch in (0, 1):
                    if q.ch_mask & (1 << ch):
                        for core in (2 * ch, 2 * ch + 1):
                            blocks.append(np.minimum(65535, sums[core] >> applied).astype("<u2").tobytes())
            body = b"".join(blocks)
            hdr = P.ReadSeqHeader(self.capture_id, q.first, q.count, 1, fmt, applied, q.ch_mask, r["Pw"], r["Tw"])
        if 16 + len(body) > P.MAX_RESPONSE_PAYLOAD:
            raise _DevErr(P.Status.E_OVERFLOW, 0, f"{16 + len(body)} > {P.MAX_RESPONSE_PAYLOAD}")
        return self._ok(req, hdr.pack() + body)

    def _read_raw_segment(self, req: P.Frame, r: P.ReadRawReq) -> P.Frame:
        """READ_RAW addr_mode 3: address = segment << 16 | word offset from the segment's record start."""
        run = self.run
        if run is None or run["mode"] != P.SEQ_SEGMENTS or not self.run_held:
            raise _DevErr(P.Status.E_STATE, 4, "no held segment run")
        addr = r.address & 0xFFFFFFFF
        seg, off = addr >> 16, addr & 0xFFFF
        slot_w = 1 << run["m"]
        if seg >= run["slots"]:
            raise _DevErr(P.Status.E_BAD_ARG, 4, f"segment {seg} >= {run['slots']} slots")
        if not 1 <= r.count <= slot_w:
            raise _DevErr(P.Status.E_BAD_ARG, 8, f"count {r.count} not in 1..{slot_w}")
        e = P.SeqEntry.decode(run["entries"][seg])
        if not e.valid:
            raise _DevErr(P.Status.E_STATE, 5, f"segment {seg} holds no record")
        ncores = bin(r.core_mask).count("1")
        if 16 + r.count * ncores > P.MAX_RESPONSE_PAYLOAD:
            raise _DevErr(P.Status.E_OVERFLOW, 0, f"{16 + r.count * ncores} > {P.MAX_RESPONSE_PAYLOAD}")
        first = (e.offset - run["Pw"] + off) % slot_w
        idx = seg * slot_w + (first + np.arange(r.count)) % slot_w
        cores = [c for c in range(4) if r.core_mask & (1 << c)]
        data = {c: self.ring[c, idx] for c in cores}
        blocks = []
        if r.layout == 0:
            blocks = [data[c].tobytes() for c in cores]
        else:
            for ch in (0, 1):
                if r.core_mask & (3 << (2 * ch)):
                    il = np.empty(2 * r.count, dtype=np.uint8)
                    lo, hi = (data[2 * ch], data[2 * ch + 1]) if r.layout == 2 else (data[2 * ch + 1], data[2 * ch])
                    il[0::2], il[1::2] = lo, hi
                    blocks.append(il.tobytes())
        hdr = P.ReadRawHeader(self.capture_id, seg * slot_w + first, slot_w, r.count, r.core_mask, r.layout,
                              int(self.acq_state), self.cfg.fw_read_lag)
        return self._ok(req, hdr.pack() + b"".join(blocks))

    # --- state machine -------------------------------------------------------------------------------

    def tick(self) -> None:
        now = self.now()
        with self.lock:
            if self.control == P.Control.REMOTE and self.watchdog_s and \
                    now - self.last_frame_at > self.watchdog_s:
                self.go_local(P.ExitReason.WATCHDOG)
            if self.shared and self.run_state == P.RunState.RUN and not self.ui_busy:
                self._ui_run_catch_up(now)
            if self.control not in (P.Control.REMOTE, P.Control.SHARED):
                return
            pc = self._pending_capture
            if pc is None or self.acq_state not in (P.AcqState.ARMED, P.AcqState.TRIGGERED) or pc.get("computing"):
                return
            scale = self.cfg.time_scale
            if pc["t_trigger"] is not None and now >= self.arm_time + scale * pc["t_trigger"]:
                self.acq_state = P.AcqState.TRIGGERED
                self.fpga_triggered = True
            if pc["t_done"] is not None and now >= self.arm_time + scale * pc["t_done"]:
                self._complete(pc)

    def _complete(self, pc: dict, origin: int = P.CaptureOrigin.HOST, n: int = 1) -> None:
        """A capture reached DONE. ``origin``: host (acq_state DONE) or UI (acq_state unchanged); ``n``: captures
        counted (the UI may have finished several since the last poll; only the latest is in the ring)."""
        self.ring = pc["ring"]
        R = RING_WORDS
        self.trigger_addr = pc["trigger_addr"]
        self.capture_start_word = (self.trigger_addr - pc["Pw"]) % R
        self.capture_geometry = (pc["Pw"], pc["Tw"])
        self.capture_samples = pc.get("samples", (2 * pc["Pw"], 2 * pc["Tw"]))
        self.capture_decim = pc.get("decim") or P.DecimConfig()
        self.capture_divider = pc["divider"]
        self.fpga_triggered = pc["triggered"]
        self.fpga_done = True
        self.auto_timeout_flag = pc["auto_timeout"]
        self.capture_id += n
        self.ring_valid = True
        self.trace_cache = None
        self.capture_settings = pc.get("settings") or self.settings.copy()
        self.capture_settings_seq = pc.get("seq", self.settings_seq)
        #: a setting changed between arm and DONE: the firmware marks the capture (protocol 1.3)
        self.capture_settings_changed = self.capture_settings_seq != self.settings_seq
        if self.capture_settings_changed:
            self.capture_settings_seq = P.SEQ_INVALID
        self.capture_origin = int(origin)
        self.capture_ext_edge = bool(pc.get("ext_edge", False))
        self.run = pc.get("run")
        self.run_held = self.run is not None
        # 1.26: the trigger hold-off keeps running past the record's end, across MCU re-arms
        tr = self.capture_settings.trigger
        self.holdoff_left_s = 0.0
        if tr.holdoff and pc.get("t_trigger") is not None and pc.get("t_done") is not None:
            self.holdoff_left_s = max(0.0, tr.holdoff * 5e-9 - (pc["t_done"] - pc["t_trigger"]))
        if self.run is None and self.capture_divider == 0 and not self.capture_decim.k:
            self._enc_check(origin)
        if origin == P.CaptureOrigin.HOST:
            self.acq_state = P.AcqState.DONE
            self._pending_capture = None
            if self.shared:
                self.run_state = P.RunState.STOP  # a host single leaves the UI in STOP (collaborative plan)

    def cap_boot(self) -> None:
        """fw 0.2.79 at boot: 0x63 = the stored value when it was trained on this bitstream (the word the FPGA
        reports), else 0x08E4."""
        self.cap_flags = P.CAPF_HAS
        self.cap_value = P.CAP_POWER_ON
        if self.cap_stored is not None:
            if self.cap_stored[1] == self.cfg.fpga_version:
                self.cap_value = self.cap_stored[0]
                self.cap_flags |= P.CAPF_TRAINED
            else:
                self.cap_flags |= P.CAPF_OTHER

    def _cap_check(self, origin: int) -> None:
        """fw 0.2.79: read 0x36 after every accepted full-rate capture; flag a core whose count exceeds its chip
        partner's by more than 8; event 23 when the set of flagged cores changes."""
        if not self.cap_flags & P.CAPF_HAS:
            return
        was = self.cap_flagged
        self.cap_counts = tuple(self.cap_counts_next.pop(0)) if self.cap_counts_next else (0, 0, 0, 0)
        self.cap_reads += 1
        self.cap_flags |= P.CAPF_READ
        self.cap_flagged = P.cap_flagged(self.cap_counts)
        if not self.cap_flagged:
            self.cap_flags &= ~P.CAPF_FLAGGED
            return
        self.cap_flags |= P.CAPF_FLAGGED
        self.cap_flagged_n += 1
        if self.cap_flagged != was:
            self.log_event(23, self.cap_flagged, max(P.cap_excess(self.cap_counts)))

    def _enc_check(self, origin: int) -> None:
        """Protocol 1.19: the firmware checks every 200 MSa/s capture (legacy layout) before anything reads it; a
        garbled one is re-armed (up to 16 times) and only the accepted capture is seen. Counters, the 0x33 byte and
        event 21 as the firmware keeps them (the capture data in the model is always clean)."""
        mode = self.enc_mode
        self.enc_checked += 1
        bad = 0x77  # 0x33 with CH2 A wrong (the mock HAL's enc33_bad)
        caller = 0 if origin != P.CaptureOrigin.HOST else 1
        value = bad if mode == 2 else 320  # the 0x33 byte, or the residual in 1/16 code (20 codes)
        last = P.ENC33_GOOD
        self.enc_flags &= ~P.ENC_LAST_BAD
        if self.enc_unrecovered_next > 0:
            self.enc_unrecovered_next -= 1
            self.enc_bad += 1
            self.enc_flags |= P.ENC_LAST_BAD
            self.log_event(21, 0x80 | (mode << 5) | 16, caller << 12 | value)
            last = bad
        elif self.enc_bad_next > 0:
            self.enc_bad_next -= 1
            self.enc_bad += 1
            self.enc_recovered += 1
            self.log_event(21, (mode << 5) | min(31, self.enc_rearms), caller << 12 | value)
        if mode == 2:
            self.enc_last33 = last
            self.enc_flags |= P.ENC_READ
        self._cap_check(origin)

    # --- collaborative mode: the simulated UI --------------------------------------------------------------

    def _ui_run_catch_up(self, now: float) -> None:
        period = self.cfg.ui_capture_period_s
        if period <= 0:
            if self.ui_done < 0:
                self.ui_done = 0
                self.ui_run_capture()
            return
        due = int((now - self.ui_t0) / period)
        if due > self.ui_done:
            n = due - self.ui_done if self.ui_done >= 0 else 1
            self.ui_done = due
            self.ui_run_capture(n)

    def ui_run_capture(self, n: int = 1) -> bool:
        """The UI loop finishes a capture with the live settings (RUN mode). In normal trigger mode without a
        trigger nothing completes. Returns whether a capture was produced."""
        with self.lock:
            pc = self.simulate_capture(self.settings)
            if pc["ring"] is None:
                if self.settings.trigger.mode == 1:
                    return False
                pc = self.simulate_capture(self.settings, forced=True)
            self._complete(pc, P.CaptureOrigin.UI_RUN, n)
            return True

    def settings_key(self, s: Settings | None = None) -> tuple:
        s = s or self.settings
        ch = tuple((c.enable, c.range, c.coupling, c.dcoffset, c.invert, c.probe_index) for c in s.channels)
        tr = s.trigger
        return ch, s.divider, (tr.source, tr.edge, tr.level, tr.mode, tr.pretrigger_samples, tr.total_samples,
                               tr.holdoff, tr.auto_timeout, tr.ttype, tr.longer, tr.n, tr.h), s.decim, s.peak

    def _bump_if_changed(self, before: tuple) -> None:
        if self.settings_key() != before:
            if self.logger is not None and self.logging:  # a setting changed at the scope ends the log
                self._log_finish(P.LOG_ERROR, self.now())
            self.settings_seq = (self.settings_seq + 1) & 0xFFFFFFFF
            if self.settings_seq == P.SEQ_INVALID:
                self.settings_seq = 0

    def ui_set_channel(self, ch: int, **fields: int) -> None:
        """Test hook: the person at the scope changes a channel setting (enable, range, coupling, dcoffset,
        invert, probe_index). Relays click like a host change."""
        with self.lock:
            before = self.settings_key()
            cs = self.settings.channels[ch]
            if "range" in fields and min(fields["range"], 5) != min(cs.range, 5) or \
                    "coupling" in fields and fields["coupling"] != cs.coupling:
                self.relay_switches += 1
                self.relay_settle_until = self.now() + 0.050
            for k, v in fields.items():
                if not hasattr(cs, k):
                    raise AttributeError(k)
                setattr(cs, k, int(v))
            self._bump_if_changed(before)

    def ui_set_timebase(self, ui_index: int) -> None:
        """Test hook: the user turns the timebase to UI index ``ui_index`` (11..34)."""
        with self.lock:
            before = self.settings_key()
            sr = P.TIME_PER_DIV_SAMPLE_RATE[ui_index]
            s = self.settings
            s.divider, s.tpd_index, s.sr_index = P.SAMPLE_RATE_DIVIDERS[sr], ui_index, sr
            self._bump_if_changed(before)

    def ui_set_trigger(self, **fields: int) -> None:
        """Test hook: the user changes trigger fields (source, edge, level, mode)."""
        with self.lock:
            before = self.settings_key()
            for k, v in fields.items():
                if not hasattr(self.settings.trigger, k):
                    raise AttributeError(k)
                setattr(self.settings.trigger, k, int(v))
            self._bump_if_changed(before)

    def ui_press_run(self) -> None:
        with self.lock:
            self._set_run_state(P.RunState.RUN)

    def ui_press_stop(self) -> None:
        with self.lock:
            self._set_run_state(P.RunState.STOP)

    def _set_run_state(self, state: int) -> None:
        """RUN/STOP from either side: a pending host single is aborted."""
        if self.acq_state in (P.AcqState.ARMED, P.AcqState.TRIGGERED):
            self.acq_state = P.AcqState.ABORTED
            self._pending_capture = None
        if self.deferred is not None:
            self._abort_deferred = True
        self.run_state = P.RunState(state)
        if state == P.RunState.RUN:
            self.ui_t0 = self.now()
            self.ui_done = 0

    def arm(self) -> None:
        self.arm_count += 1
        if self.on_arm is not None:
            self.on_arm(self)
        now = self.now()
        self.ring_valid = False
        if self.shared:
            self.run_state = P.RunState.SINGLE
        if now < self.relay_settle_until:
            # firmware waits out the relay settling (8.4 step 1); in the model the ARM just starts later
            now = self.relay_settle_until
        self.arm_time = now
        self.acq_state = P.AcqState.ARMED
        self.fpga_triggered = False
        self.fpga_done = False
        self.auto_timeout_flag = False
        self.run_held = False
        if self.holdoff_clear_pending:  # 1.26: 0x66 = 1 written by SET_TRIGGER / SET_TRIGGER2: this 0x01 = 1 clears the timer
            self.holdoff_left_s = 0.0
            self.holdoff_clear_pending = False
        tr0 = self.settings.trigger  # the qualifier status words (GET_STATUS 48..51) at the arm (fpga_model's reset state)
        self.trig_status = (0, (tr0.ttype & 3) | (P.STATUS34_LONGER if tr0.longer else 0) |
                            (P.STATUS34_EXT if tr0.source == 2 else 0) |
                            (P.STATUS34_COUNTING if tr0.ttype != P.TRIG_EDGE and tr0.h == 0 else 0),
                            max(0, (tr0.n & 0xFFFF) - 1) if tr0.ttype == P.TRIG_NTH_EDGE else 0)
        if self.seq_setting.mode != P.SEQ_OFF:
            self._start_run(self.settings.copy(), self.seq_setting)
        else:
            self._pending_capture = self.simulate_capture(self.settings)
            if self.flat_next and self._pending_capture.get("ring") is not None:
                self.flat_next -= 1
                self._pending_capture["ring"] = np.full_like(self._pending_capture["ring"], 0xFF)
        self.decim_written = self.decim_config()
        self.tick()

    def _start_run(self, settings: Settings, sq: P.SetSeqReq) -> None:
        """A run is modelled record by record, which takes long for many records: off the request path, like the
        FPGA. Until the model is ready the run is armed (``computing``); STOP waits for it."""
        gen = self.arm_count
        self._pending_capture = {"run": True, "computing": True, "t_trigger": None, "t_done": None}

        def work() -> None:
            pc = self.simulate_run(settings, sq)
            with self.lock:
                if self.arm_count == gen and self._pending_capture is not None and \
                        self._pending_capture.get("computing"):
                    self._pending_capture = pc
                    self.tick()

        self._run_thread = threading.Thread(target=work, name="sim-seq-run", daemon=True)
        self._run_thread.start()

    def wait_run_model(self, timeout: float = 60.0) -> None:
        """Test helper and STOP: wait until the pending run's model is computed."""
        th = getattr(self, "_run_thread", None)
        if th is not None and th is not threading.current_thread():
            th.join(timeout)

    def force(self) -> None:
        pc = self.simulate_capture(self.settings, forced=True)
        self._complete(pc)

    def go_remote(self, flags: int, watchdog_s: int) -> None:
        if self.shared:
            return  # protocol 1.3: SET_REMOTE is a no-op
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
            self.settings = self.snapshot
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
        if self.shared:
            return self._shared_status(now)
        if self.control == P.Control.LOCAL and not self.ui_busy and self.cfg.ui_capture_period_s > 0:
            # the UI keeps capturing
            t0 = getattr(self, "ui_t0", self.t_boot)
            self.capture_id = self.ui_capture_base + int((now - t0) / self.cfg.ui_capture_period_s)
        fl = 0
        remote = self.control == P.Control.REMOTE  # 14.10: the FPGA flags read 0 in LOCAL
        if remote and self.fpga_triggered:
            fl |= P.StatusFlags.FPGA_TRIGGERED
        if remote and self.fpga_done:
            fl |= P.StatusFlags.FPGA_DONE
        if remote and self.auto_timeout_flag:
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
            battery_level=18, exit_reason=self.exit_reason, pass_ms=self.pass_ms, pass_max_ms=self.pass_max_ms)

    def _shared_status(self, now: float) -> P.StatusResp:
        fl = 0
        for on, bit in ((self.fpga_triggered, P.StatusFlags.FPGA_TRIGGERED), (self.fpga_done, P.StatusFlags.FPGA_DONE),
                        (self.auto_timeout_flag, P.StatusFlags.AUTO_TIMEOUT), (self.dtr, P.StatusFlags.DTR),
                        (self.deferred is not None, P.StatusFlags.DEFERRED_PENDING),
                        (self.overload[0], P.StatusFlags.OVERLOAD_CH1), (self.overload[1], P.StatusFlags.OVERLOAD_CH2)):
            if on:
                fl |= bit
        return P.StatusResp(
            control=int(P.Control.SHARED), acq_state=int(self.acq_state), flags=int(fl), ui_busy=int(self.ui_busy),
            capture_id=self.capture_id & 0xFFFFFFFF, trigger_addr=self.trigger_addr,
            capture_start_word=self.capture_start_word, ms_since_arm=int((now - self.arm_time) * 1000) & 0xFFFFFFFF,
            uptime_ms=int((now - self.t_boot) * 1000) & 0xFFFFFFFF, watchdog_remaining_s=0xFFFF, battery_level=18,
            exit_reason=0, pass_ms=self.pass_ms, pass_max_ms=self.pass_max_ms, settings_seq=self.settings_seq,
            capture_settings_seq=self.capture_settings_seq if self.capture_id else P.SEQ_INVALID,
            run_state=int(self.run_state), capture_origin=self.capture_origin if self.capture_id else 0,
            ext_flags=self._ext_flags(now), display_mode=int(self.display_mode), features=int(self.features),
            ui_average=self.ui_average_state()[0], ui_average_log2n=self.ui_average_state()[1],
            fpga_37=self.trig_status[0] | (P.STATUS37_HOLDOFF_RUNNING if self.holdoff_left_s > 0 else 0),
            fpga_34=self.trig_status[1], edges_left=self.trig_status[2])

    def _ext_flags(self, now: float) -> int:
        if not self.caps & P.Caps.EXT_TRIGGER:
            return 0
        sig = self.cfg.ext_signal or self.cfg.signals[1]
        level = float(sig.eval(np.array([now - self.t_boot]), self.settings.gen)[0]) > EXT_THRESHOLD_V
        return (int(P.ExtFlags.LEVEL) if level else 0) | (int(P.ExtFlags.GENERATOR) if self.settings.gen.enable else 0) \
            | (int(P.ExtFlags.EDGE_SEEN) if getattr(self, "capture_ext_edge", False) and self.ring_valid else 0)

    def status_bytes(self) -> bytes:
        """GET_STATUS body as this firmware sends it: 32 bytes in the legacy (protocol 1.2) model, 48 in 1.17..1.25, 52
        since 1.26."""
        b = self.status().pack()
        if not self.shared:
            return b[:32]
        return b if self.trig26_on else b[:48]

    @property
    def cap_on(self) -> bool:
        """Protocol 1.24 (fw 0.2.79): the firmware drives the 0x153A capture stages (caps bit 19, features bit 7, the
        longer EXT shifts). From the config, not ``proto_minor`` (which needs ``shared``, which needs the caps)."""
        return not self.cfg.legacy and (self.cfg.proto_minor is None or self.cfg.proto_minor >= 24)

    @property
    def proto_minor(self) -> int:
        if not self.shared:
            return 2  # fw 0.2.12 = 1.5 (1.2 model in legacy)
        return P.PROTO_MINOR if self.cfg.proto_minor is None else self.cfg.proto_minor

    def _ui_yield(self) -> Callable[[P.Frame], P.Frame] | None:
        """A request that needs the main loop while the UI is modal. Protocol 1.23 (fw 0.2.78): the modal screens close by
        their cancel paths and it runs (None), unless a step holds them (E_BUSY with the reason); STARTING: the main loop
        is not up yet (E_BUSY detail 2 after the firmware's 1.5 s). Older firmware: E_BUSY "UI busy"."""
        if self.proto_minor < P.YIELD_MINOR:
            return lambda req: self._err(req, P.Status.E_BUSY, 0, "UI busy")
        why = self.ui_hold or (P.BusyDetail.NO_YIELD if self.ui_busy == P.UiBusy.STARTING else 0)
        if why:
            self.ui_yield_refused += 1
            return lambda req: self._err(req, P.Status.E_BUSY, int(why), P.BUSY_TEXTS[why])
        self.ui_busy = False
        self.ui_yields += 1
        return None

    def capture_info(self) -> P.CaptureInfo:
        s = self.capture_settings
        Pw, Tw = self.capture_geometry
        fl = (P.CaptureInfoHeader.F_TRIGGERED if self.fpga_triggered else 0) | \
             (P.CaptureInfoHeader.F_AUTO_TIMEOUT if self.auto_timeout_flag else 0) | \
             (P.CaptureInfoHeader.F_OVERLOAD_CH1 if self.overload[0] else 0) | \
             (P.CaptureInfoHeader.F_OVERLOAD_CH2 if self.overload[1] else 0) | \
             (P.CaptureInfoHeader.F_SETTINGS_CHANGED if getattr(self, "capture_settings_changed", False) else 0) | \
             (P.CaptureInfoHeader.F_EXT_EDGE if getattr(self, "capture_ext_edge", False) else 0)
        tr = s.trigger
        pre, total = self.capture_samples
        h = P.CaptureInfoHeader(self.capture_id & 0xFFFFFFFF, self.capture_settings_seq, self.capture_origin, fl,
                                self.trigger_addr, self.capture_start_word, self.capture_divider, s.tpd_index,
                                tr.source, tr.edge, tr.level, tr.mode, pre, total)
        chans = []
        for ch in (0, 1):
            cc = self.channel_config(ch, s)
            chans.append(P.CaptureChannelInfo(cc.enable, cc.range, cc.coupling, cc.invert, cc.probe_index,
                                              cc.offset_pwm, cc.uv_per_lsb_q8, cc.zero_code_q8))
        d = self.capture_decim
        return P.CaptureInfo(h, chans, d.status, d.shift_byte if self.trig26_on else d.shift,
                             self.ext_shift_for(d, self.capture_divider, tr.source, pre), d.n, has_decim=True,
                             run=self.run_info(), trig2=tr.capture_trig2() if self.trig26_on else None)

    def run_info(self) -> P.SeqRunInfo:
        """GET_CAPTURE_INFO 76..95 (protocol 1.17) of the latest capture; since 1.26 byte 79 also says 16-bit / peak for
        single captures."""
        r = self.run
        d = self.capture_decim
        wide = ((P.CAPINFO_WIDE if d.k else 0) | (P.CAPINFO_PEAK if d.peak else 0)) if self.trig26_on else 0
        if r is None:
            return P.SeqRunInfo(wide=wide)
        return P.SeqRunInfo(r["mode"], r["run_flags"], r["m"], int(r["wide"]) | wide, r["count"], r["untriggered"],
                            r["requested"], r["n"], r["holdoff_ns"])

    def info(self) -> P.InfoResp:
        """GET_INFO: the firmware's fixed geometry and caps whatever word the FPGA reports (fw 0.2.102); 1.25 adds the
        built-for word and the mismatch flag in 76..79 (a model of older firmware answers 76 bytes)."""
        i = P.InfoResp(proto_minor=self.proto_minor,
            fpga_version=self.cfg.fpga_version, fpga_variant=int(P.FpgaVariant.AL3), ring_words=RING_WORDS,
            caps=self.caps, gen_clock_hz=P.GEN_CLOCK_HZ, max_total_samples=MAX_TOTAL_SAMPLES, min_total_samples=80,
            read_lag=self.cfg.fw_read_lag, interleave_order=0, fw_version=self.cfg.fw_version.encode(),
            fw_build=SIM_BUILD, serial=self.cfg.serial.encode())
        if self.proto_minor >= 25:
            i.fw_fpga_version, i.fpga_flags = FPGA_VERSION, self.fpga_flags
        return i

    def info_bytes(self) -> bytes:
        return self.info().pack()[: 80 if self.proto_minor >= 25 else 76]

    def timebase(self) -> P.Timebase:
        s = self.settings
        tb = P.timebase_for_divider(s.divider, s.tpd_index, s.sr_index)
        dec = self.decim_config()
        if dec.k:  # 1.9: the output period N x 5 ns
            tb.sample_rate_hz, tb.sample_period_ps = P.BASE_CLOCK_HZ // dec.n, min(5000 * dec.n, 0xFFFFFFFF)
        return tb

    def trigger(self) -> P.Trigger:
        tr = self.settings.trigger
        Pw, Tw, auto_to = self.geometry()
        pre, total = tr.pretrigger_samples, tr.total_samples
        per = 1 if self.decim_config().k else 2
        return P.Trigger(source=tr.source, edge=tr.edge, level=tr.level, mode=tr.mode,
                         pretrigger_samples=pre, total_samples=total, holdoff=tr.holdoff,
                         auto_timeout=auto_to, pretrigger_words=pre // per, total_words=total // per,
                         trigger2=tr.trigger2() if self.trig26_on else None)

    def gen(self) -> P.Gen:
        """GEN (40 bytes, protocol 1.27): the square's P16 / H16 (0 while a SET_PATTERN pattern drives the pin) and
        the pattern status read live (0x39)."""
        g = self.settings.gen
        st0, bursts, st3 = self.pattern_status()
        sq = g.source == P.GEN_SOURCE_SQUARE
        return P.Gen(enable=g.enable, pat_status=st0, bursts=bursts, pat_status3=st3, source=g.source,
                     freq_mhz_actual=P.gen_freq_mhz(g.p16) if sq else 0,
                     duty_bp_actual=P.gen_duty_bp(g.h16, g.p16) if sq else 0,
                     p16=g.p16 if sq else 0, h16=g.h16 if sq else 0)

    def pattern_status(self) -> tuple[int, int, int]:
        """FPGA 0x39 now: (byte 0 running / in gap / pending / step, bursts done, byte 3 marker seen / mode active).
        The generator's square runs whenever the pin is driven; a pattern runs from its MCU start (device time)."""
        now = self.now()
        g = self.settings.gen
        pend = P.PAT_ST_PENDING if now < self.pat_pending_until else 0
        if g.source == P.GEN_SOURCE_SQUARE:
            if not g.enable or not g.p16:
                return pend, 0, 0
            f = g.frequency_hz
            step = 0 if ((now - self.t_boot) * f) % 1.0 < g.duty else 1
            return P.PAT_ST_RUNNING | pend | step, 0, P.PAT_ST3_ACTIVE
        pat = g.pattern
        if pat is None or pat.mode == P.PAT_OFF:
            return pend, 0, 0
        st3 = P.PAT_ST3_ACTIVE | (P.PAT_ST3_MARKER if self.pat_marker_seen else 0)
        if self.pat_start_dev is None:
            return pend, 0, st3
        c = int((now - self.pat_start_dev) * P.PAT_CLOCK_HZ)
        running, gap, step, bursts = pat.state_at(c)  # the marker bit: a marker inside an armed capture (0x01 = 1)
        return (P.PAT_ST_RUNNING if running else 0) | (P.PAT_ST_GAP if gap else 0) | pend | (step & 0x0F), \
            min(bursts, 0xFFFF), st3

    # --- readout --------------------------------------------------------------------------------------

    def read_ring(self, core: int, start: int, count: int, *, lag_applied: int | None = None) -> np.ndarray:
        """READ_RAW semantics: exact addresses when the firmware's read_lag equals the FPGA's."""
        R = RING_WORDS
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

    def build_trace16(self, ch: int) -> np.ndarray:
        """1.9: the 16-bit samples of a decimated capture as stored (low byte core A, high byte core B)."""
        Tw = self.capture_geometry[1]
        lo = self.read_ring(2 * ch, self.capture_start_word, Tw, lag_applied=self.cfg.hw_read_lag).astype(np.uint16)
        hi = self.read_ring(2 * ch + 1, self.capture_start_word, Tw, lag_applied=self.cfg.hw_read_lag)
        return lo | (hi.astype(np.uint16) << 8)

    def build_trace(self, ch: int, settings: Settings) -> tuple[np.ndarray, int]:
        """Processed trace of one channel (section 7.6); before 1.22 with the 1.00o5 stale first byte per core. 1.9 decimated
        capture: the converted codes round(sample x 2^S / N^K + (adc_a_comp + adc_b_comp) / 2), 0..255."""
        Pw, Tw = self.capture_geometry
        dec = self.capture_decim
        if dec.k and dec.peak:  # 1.26: the midpoint of { max, min } through the full class's mean-core correction
            w = self.build_trace16(ch).astype(np.int64)
            tr = peak_mid_codes(w & 0xFF, w >> 8, *self._peak_corr(ch, settings))
            return tr, (1 if settings.channels[ch].invert else 0)
        if dec.k:
            out = self.build_trace16(ch).astype(np.int64)
            gain16, corr16 = self._corr16(ch, settings)
            scale = (dec.scale_q24 * gain16 + 32768) >> 16  # acal_scale
            tr = np.clip((out * scale + (corr16 << 20) + (1 << 23)) >> 24, 0, 255).astype(np.uint8)
            return tr, (1 if settings.channels[ch].invert else 0)
        start = self.capture_start_word
        a = self.read_ring(2 * ch, start, Tw, lag_applied=self.cfg.hw_read_lag).astype(np.int16)
        b = self.read_ring(2 * ch + 1, start, Tw, lag_applied=self.cfg.hw_read_lag).astype(np.int16)
        # stale pipeline byte: the first byte after the pointer write is off by about +-1 code. fw 0.2.72 (1.22) makes
        # the dummy strobe in every readout, so READ_TRACE is exact like READ_RAW
        a = a.copy()
        b = b.copy()
        if self.proto_minor < 22:
            a[0] = np.clip(a[0] + 1, 0, 255)
            b[0] = np.clip(b[0] - 1, 0, 255)
        raw_clip0 = (a == 0) | (b == 0)
        raw_clip255 = (a == 255) | (b == 255)
        cal = self.cal[ch]
        if self.cfg.cal_v3:  # fw 0.2.45: code = (raw x gain + off + u) >> 16, u a 16-bit dither
            c = self._v3_corr(ch, settings)
            u = np.random.default_rng(self.capture_id).integers(0, 65536, (2, a.size), dtype=np.int64)
            ac = np.clip((a.astype(np.int64) * c["gain"][0] + c["off"][0] + u[0]) >> 16, 0, 255).astype(np.int16)
            bc = np.clip((b.astype(np.int64) * c["gain"][1] + c["off"][1] + u[1]) >> 16, 0, 255).astype(np.int16)
        else:
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
            self.link["frames"] += 1
            if self.last_resp and self.last_resp[0] == req.seq and self.last_resp[1] == req.cmd:
                self.link["duplicates"] += 1
                return P.decode_frame(self.last_resp[2])
            if req.cmd in P.RETIRED_CMDS:  # 1.27: READ_LIVE's code answers as the firmware's NULL handler does
                return self._done(req, self._err(req, P.Status.E_UNSUPPORTED, 0, "capability"))
            try:
                cmd = P.Cmd(req.cmd)
            except ValueError:
                return self._done(req, self._err(req, P.Status.E_UNKNOWN_CMD, 0, f"cmd 0x{req.cmd:04X}"))
            if not self.shared and cmd in (P.Cmd.SET_ACQ_STATE, P.Cmd.GET_CAPTURE_INFO, P.Cmd.READ_SPECTRUM,
                                           P.Cmd.FFT_BENCH, P.Cmd.GET_DECIM, P.Cmd.SET_DECIM, P.Cmd.GET_COUNTER,
                                           P.Cmd.SET_TRIGGER2):
                # protocol 1.2 firmware does not know the 1.3 commands
                return self._done(req, self._err(req, P.Status.E_UNKNOWN_CMD, 0, f"cmd 0x{req.cmd:04X}"))
            if self.mismatch and cmd not in P.BASIC_CMDS:
                # 1.25 (fw 0.2.102): the scope does not run the bitstream the firmware is built for; nothing that would
                # configure or read the acquisition runs (pc_dispatch), the basic set keeps the link and the diagnostics
                return self._done(req, self._err(req, P.Status.E_UNSUPPORTED, P.UNSUP_FPGA_MISMATCH,
                                                 P.FPGA_MISMATCH_TEXT))
            if self.deferred is not None and self.deferred.frame.seq == req.seq and                     self.deferred.frame.cmd == req.cmd:
                return None  # retransmission of the pending deferred request: its response follows
            if self.deferred is not None and cmd not in P.CONTROL_CMDS:
                return self._done(req, self._err(req, P.Status.E_BUSY, 0, "deferred request pending"))
            if self.ui_busy and cmd not in P.BUSY_ALLOWED:
                refused = self._ui_yield()
                if refused is not None:
                    return self._done(req, refused(req))
            if not self.shared and cmd not in P.LOCAL_ALLOWED and self.control != P.Control.REMOTE:
                return self._done(req, self._err(req, P.Status.E_NOT_REMOTE, 0, "acquire REMOTE first"))
            need = P.CMD_CAP.get(cmd)
            if need is not None and not (self.caps & need):
                return self._done(req, self._err(req, P.Status.E_UNSUPPORTED, 0, f"cap {need.name} missing"))
            if cmd not in P.LOG_ALLOWED and self.logging:
                return self._done(req, self._err(req, P.Status.E_STATE, P.LOG_E_STATE_LOGGING,
                                                 "logging: LOG_STOP first"))
            size = P.REQUEST_SIZES.get(cmd)
            if size is not None and len(req.payload) < size:
                # protocol 5 / 14.3: missing trailing fields read as zero (no E_BAD_LEN)
                req = P.Frame(req.cmd, req.seq, req.payload + b"\0" * (size - len(req.payload)), req.status,
                              req.flags, req.proto_major)
            handler = getattr(self, f"_cmd_{cmd.name.lower()}")
            before = self.settings_key()
            try:
                out = handler(req)
            except _DevErr as e:
                out = self._err(req, e.status, e.detail, e.text)
            if self.shared:
                self._bump_if_changed(before)
            if out is None:
                return None
            return self._done(req, out)

    def _done(self, req: P.Frame, resp: P.Frame) -> P.Frame:
        enc = resp.encode()
        if resp.status == P.Status.OK and len(resp.payload) <= 256:
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
                return self._done(d.frame, self._ok(d.frame, self.status_bytes()))
            if now >= d.done_at:
                self.deferred = None
                status, payload = d.result()
                return self._done(d.frame, P.response(d.frame, payload, status))
            return None

    # --- command handlers ------------------------------------------------------------------------------

    def _cmd_ping(self, req: P.Frame) -> P.Frame:
        return self._ok(req, req.payload)

    def _cmd_get_info(self, req: P.Frame) -> P.Frame:
        return self._ok(req, self.info_bytes())

    def _cmd_get_status(self, req: P.Frame) -> P.Frame:
        return self._ok(req, self.status_bytes())

    def _cmd_set_remote(self, req: P.Frame) -> P.Frame:
        r = P.SetRemoteReq.unpack(req.payload)
        if r.action == 1:
            if self.ui_busy:
                raise _DevErr(P.Status.E_BUSY, 0, "UI is modal")
            self.go_remote(r.flags, r.watchdog_s)
        elif r.action == 0:
            if self.control == P.Control.REMOTE:
                # 14.11: a host release uses its own KEEP_SETTINGS flag; other exits use the acquire-time flag
                self.go_local(P.ExitReason.HOST_RELEASE, keep=bool(r.flags & P.SetRemoteReq.KEEP_SETTINGS))
        else:
            raise _DevErr(P.Status.E_BAD_ARG, 0, f"action {r.action}")
        return self._ok(req, self.status_bytes())

    def _cmd_get_link_stats(self, req: P.Frame) -> P.Frame:
        return self._ok(req, P.pack_link_stats(self.link))

    def _cmd_get_event_log(self, req: P.Frame) -> P.Frame:
        if self.proto_minor < 5:
            raise _DevErr(P.Status.E_UNKNOWN_CMD, 0, "cmd 0x0009")
        now = int((self.now() - self.t_boot) * 1000)
        flags = req.payload[0] if req.payload else 0
        trace_bits = P.EVENT_LOG_FPGA_TRACE_ON | P.EVENT_LOG_FPGA_TRACE_OFF
        allowed = P.EVENT_LOG_REARM | (trace_bits if self.proto_minor >= 22 else 0)
        if (flags & ~allowed) or (flags & trace_bits) == trace_bits:
            raise _DevErr(P.Status.E_BAD_ARG, 0, "flags")
        if flags & trace_bits:  # 1.22: the FPGA write trace level (event type 22)
            self.fpga_trace = bool(flags & P.EVENT_LOG_FPGA_TRACE_ON)
        log = P.EventLog(self.event_total, now, list(self.events[-128:]), frozen=self.ev_frozen,
                         reset_ms=self.ev_reset_ms, snapshot=list(self.ev_snapshot), has_snapshot=True,
                         bus_events=list(self.bus_events), bus_total=self.bus_total, has_bus_ring=True,
                         fpga_trace=self.fpga_trace)
        if flags & P.EVENT_LOG_REARM:  # fw 0.2.18: only an explicit re-arm clears the snapshot
            self.ev_frozen, self.ev_snapshot, self.ev_reset_ms = False, [], 0
        return self._ok(req, P.pack_event_log(log))

    def self_restart(self) -> None:
        """Test hook: the firmware restarts itself from RAM (bench 0.2.12 / 0.2.16): uptime and the event ring
        start over; the connection stays."""
        with self.lock:
            self.t_boot = self.now()
            self.events, self.event_total = [], 0
            self.bus_events, self.bus_total = [], 0
            self.ev_frozen, self.ev_snapshot, self.ev_reset_ms = False, [], 0

    def log_event(self, type_: int, a: int = 0, b: int = 0) -> None:
        """Record a USB/link event for GET_EVENT_LOG (test hook too). fw 0.2.15: ring of 128; the first bus reset
        after a read freezes the 16 events before it."""
        ms = int((self.now() - self.t_boot) * 1000)
        if type_ == 1 and not self.ev_frozen:
            self.ev_frozen, self.ev_reset_ms, self.ev_snapshot = True, ms, list(self.events[-16:])
        self.events.append(P.DeviceEvent(ms, type_, a, b))
        del self.events[:-128]
        self.event_total += 1
        if type_ in P.BUS_EVENT_TYPES:
            self.bus_events.append(self.events[-1])
            del self.bus_events[:-16]
            self.bus_total += 1

    # --- spectrum view (fw 0.2.28) --------------------------------------------------------------------------

    def ui_set_display(self, mode: int) -> None:
        """Test hook: the person switches the display mode (P.DisplayMode: 2 / 3 spectrum sub-views, 4 phosphor)."""
        with self.lock:
            self.display_mode = P.DisplayMode(mode)

    def ui_set_spectrum(self, on: bool = True, *, window: int | None = None, log2n: int | None = None,
                        channel: int | None = None) -> None:
        """Test hook: the person opens / closes the spectrum view or changes its window, FFT length or channel."""
        with self.lock:
            self.display_mode = P.DisplayMode.SPECTRUM if on else P.DisplayMode.SCOPE
            if window is not None:
                self.spec_window = window
            if log2n is not None:
                self.spec_log2n = log2n
            if channel is not None:
                self.spec_channel = channel

    def _spectra(self) -> dict[int, tuple[P.SpectrumHeader, np.ndarray]]:
        """The spectra of the latest capture as the view computes them (recomputed on a new capture or setting)."""
        s = self.capture_settings
        n = self.capture_samples[1]
        lmax = 10
        while lmax < 14 and (2 << lmax) <= n:
            lmax += 1
        log2n = min(self.spec_log2n, lmax)
        key = (self.capture_id, self.capture_settings_seq, self.spec_window, log2n, self.spec_channel)
        if key == self._spec_key:
            return self._spec
        N = 1 << log2n
        win = P.SPECTRUM_WINDOWS[self.spec_window]
        out = {}
        for ch in (0, 1):
            cs = s.channels[ch]
            if not cs.enable or self.spec_channel not in (2, ch):
                continue
            tr, _ = self.build_trace(ch, s)
            dsv = self.cal[ch].dc_shift_value
            zero = 128 - int(cs.dcoffset * 100 / dsv) if dsv else 128  # C division truncates
            cfg = self.channel_config(ch, s)
            gain = round(2000 * math.log10(cfg.uv_per_lsb_q8 / 256e6 * cfg.probe_factor))
            with np.errstate(invalid="ignore"):
                lv = np.round(analysis.scope_fft_db(tr, zero, win, N) * 100) + gain
            lv = np.clip(np.nan_to_num(lv, nan=P.SPECTRUM_EMPTY, neginf=P.SPECTRUM_EMPTY), P.SPECTRUM_EMPTY, 32000)
            bw = (200_000_000_000 // (self.capture_divider + 1)) >> log2n
            h = P.SpectrumHeader(ch, self.spec_window, log2n, 0, 0, self.capture_divider,
                                 (200_000_000 + (self.capture_divider + 1) // 2) // (self.capture_divider + 1), bw, gain,
                                 N // 2 + 1, 0, 0, {0: 1000, 1: 1500, 2: 3770}[self.spec_window])
            out[ch] = (h, lv.astype(np.int16))
        self.spec_frames += 1
        self._spec_key, self._spec = key, out
        return out

    def _cmd_fft_bench(self, req: P.Frame) -> P.Frame:
        """FFT_BENCH (fw 0.2.27, entries as 0.2.31): the CH1 trace buffer as codes - 128, Hann; plausible ARM926 timings."""
        reps = req.payload[0] if req.payload else 0
        if reps > 16:
            raise _DevErr(P.Status.E_BAD_ARG, 0, "reps")
        reps = reps or 3
        tr = np.full(16384, 128, dtype=np.float64)
        if self.capture_id and self.ring_valid:
            t, _ = self.build_trace(0, self.capture_settings)
            tr[:min(t.size, tr.size)] = t[:tr.size]
        out = []
        # fw 0.2.31: the plan holds twiddles too, so on-chip SRAM fits N <= 2048 only (row 44)
        for log2n, sram in ((10, 0), (11, 0), (12, 0), (14, 0), (10, 1), (11, 1)):
            n = 1 << log2n
            db = analysis.scope_fft_db(tr, 128, "hann", n)
            k = int(np.argmax(np.where(np.isfinite(db[2:]), db[2:], -1e9))) + 2
            run = int(n * log2n * (0.11 if sram else 0.16))
            out.append(P.FftBenchEntry(n, 1, sram, n // 3, run, run + 900, n // 2, k,
                                       int(round(max(db[k], -320.0) * 100))))
        return self._ok(req, P.pack_fft_bench(P.FftBench(reps, 0x00051078, out)))

    def decim_state(self) -> P.DecimState:
        s = self.settings
        nxt = self.decim_config()
        last = self.decim_written
        filt = P.DECIM_PEAK if s.peak and self.trig26_on else s.decim & 3
        sb = (lambda d: d.shift_byte) if self.trig26_on else (lambda d: d.shift)  # 1.26: the peak bits in the shift byte
        flags = s.decim & (0 if filt == P.DECIM_PEAK else P.DECIM_SET_FIR)  # the kept FIR masked
        return P.DecimState(filt, flags >> 2, nxt.status, sb(nxt), nxt.n, nxt.gain,
                            s.divider, nxt.rate(s.divider), last.status if last else 0xFF, sb(last) if last else 0,
                            last.status if last else 0, sb(last) if last else 0, P.DECIM_MAX_TOTAL, nxt.why,
                            self.ext_shift_for(nxt, s.divider, s.trigger.source, s.trigger.pretrigger_samples),
                            int(self.features),
                            *self.ui_average_state())

    def _cmd_get_decim(self, req: P.Frame) -> P.Frame:
        return self._ok(req, self.decim_state().pack())

    def _cmd_set_decim(self, req: P.Frame) -> P.Frame:
        r = P.SetDecimReq.unpack(req.payload)
        if r.filter > P.DECIM_PEAK:
            raise _DevErr(P.Status.E_BAD_ARG, 0, "filter 0..3, 4 peak")
        if r.filter == P.DECIM_PEAK and not (self.caps & P.Caps.PEAK_DETECT):
            raise _DevErr(P.Status.E_UNSUPPORTED, 0, "peak detect needs caps PEAK_DETECT (protocol 1.26)")
        if r.flags & ~0x0F:  # bit 3 the FIR (1.11); bits 0-2 (swap, falling edge) accepted and dropped (1.27)
            raise _DevErr(P.Status.E_BAD_ARG, 1, "flags")
        if r.filter == P.DECIM_PEAK and r.flags & P.SetDecimReq.FIR:
            raise _DevErr(P.Status.E_BAD_ARG, 1, "FIR with peak detect")
        if r.filter and self.settings.trigger.total_samples > P.DECIM_MAX_TOTAL:
            raise _DevErr(P.Status.E_BAD_ARG, 0, "total > 8190: SET_TRIGGER total first")
        if r.filter == P.DECIM_PEAK:  # 1.26: the Sample row; the Filter word keeps its K and FIR for the way back
            self.settings.peak = 1
            self.settings.decim = self.settings.decim & (3 | P.DECIM_SET_FIR)
        else:
            self.settings.peak = 0
            self.settings.decim = r.filter | ((r.flags & P.SetDecimReq.FIR) << 2)
        return self._ok(req, self.decim_state().pack())

    def ui_set_decim(self, filter_: int) -> None:
        """Test hook: the person sets the acquisition Filter at the scope."""
        with self.lock:
            before = self.settings_key()
            self.settings.decim = filter_
            self._bump_if_changed(before)

    def _cmd_read_spectrum(self, req: P.Frame) -> P.Frame:
        r = P.ReadSpectrumReq.unpack(req.payload)
        if r.channel > 1:
            raise _DevErr(P.Status.E_BAD_ARG, 0, "channel")
        if self.display_mode not in (1, 2, 3) or self.capture_id == 0:
            raise _DevErr(P.Status.E_STATE, 0, "no spectrum shown for this channel")
        try:
            self._check_capture_readable()
            spectra = self._spectra()
        except _DevErr:
            spectra = self._spec  # the view keeps showing the last spectrum it computed
        if r.channel not in spectra:
            raise _DevErr(P.Status.E_STATE, 0, "no spectrum shown for this channel")
        h0, lv = spectra[r.channel]
        mx = (P.MAX_RESPONSE_PAYLOAD - 32) // 2
        count = mx if r.count == 0 or r.count > mx else r.count
        first = min(r.first, h0.bins)
        n = min(count, h0.bins - first)
        h = replace(h0, frame=self.spec_frames, first=first, count=n)
        return self._ok(req, P.pack_spectrum(h, lv[first: first + n].tolist()))

    def _cmd_cal_probe(self, req: P.Frame) -> P.Frame:
        """Protocol 1.21 (row 61): the wizard's zero measurement of one channel and range, on the model's front end
        (offset formula, per-core zero errors, the channel's signal and noise). Checks modelled: CLIP (a sample at 0 or
        255 in a fine point) and NOISE (RMS about the mean over 3 codes); ``cal_probe_fail`` = (point, rc) forces a
        failure for host tests."""
        r = P.CalProbeReq.unpack(req.payload)
        for v, lim, det in ((r.channel, 1, 0), (r.range, 5, 1), (r.settle, 15, 2), (r.prestrobe, 1, 3), (r.mode, 1, 4)):
            if v > lim:
                raise _DevErr(P.Status.E_BAD_ARG, det, "channel / range / settle / prestrobe / mode")
        if self.acq_state in (P.AcqState.ARMED, P.AcqState.TRIGGERED):
            raise _DevErr(P.Status.E_STATE, 0, "a host capture is armed")
        if self.shared and self.run_state == P.RunState.RUN:
            raise _DevErr(P.Status.E_STATE, 1, "the UI acquisition runs: SET_ACQ_STATE 0 first")
        flags = (r.settle << 4) | (P.CALP_PRESTROBE if r.prestrobe else 0) | (P.CALP_MODE1 if r.mode else 0)
        if r.mode:
            out = None
            for rng in range(6):
                out = self._cal_probe_range(r.channel, rng, flags)
                if out.rc:
                    break
            assert out is not None
        else:
            out = self._cal_probe_range(r.channel, r.range, flags)
        return self._ok(req, out.pack())

    def _cal_probe_range(self, ch: int, rng: int, flags: int) -> P.CalProbeResp:
        cal = self.cal[ch]

        def measure(pwm: int, gated: bool, convs: int) -> P.CalProbePoint:
            s = self.settings.copy()
            s.channels[ch].range = rng
            s.channels[ch].coupling = 0
            s.channels[ch].dcoffset = pwm - cal.dc_calibration_offset[rng]
            s.divider = 1 if gated else 0
            s.decim = 0
            dt = self.dt(s.divider)
            cores = []
            for core in ("A", "B"):
                caps = [self._codes(ch, self.now() + i * 1.37e-4 + np.arange(500) * dt * 2, core, s).astype(np.float64)
                        for i in range(convs)]
                allc = np.concatenate(caps)
                cores.append(P.CalProbeCore(float(allc.mean()), float(allc.std()),
                                            float(np.sqrt(np.mean([c.var() for c in caps]))), int(allc.min()),
                                            int(allc.max()), int(np.sum((allc == 0) | (allc == 255)))))
            return P.CalProbePoint(0, pwm, 1 if gated else 0, convs, (cores[0], cores[1]))

        def mean(p: P.CalProbePoint) -> float:
            return (p.cores[0].mean + p.cores[1].mean) / 2

        hi, lo = measure(500, True, 2), measure(1200, True, 2)
        slope = (mean(lo) - mean(hi)) / 700.0  # codes per PWM count (negative)
        pts: list[P.CalProbePoint] = [hi, lo]
        centre = int(round(500 + (128.0 - mean(hi)) / slope)) if slope else 0
        if slope >= 0 or not 0 < centre < 2000:
            return P.CalProbeResp(ch, rng, 4, 0, 1, 0, flags | P.CALP_NO_CENTRE, 0, 0, 0, 0, 0, 0,
                                  self._cal_points(pts), 0)
        sl, sh = measure(centre - 150, True, 4), measure(centre + 150, True, 4)
        gslope = (mean(sh) - mean(sl)) / 300.0
        zero = int(round(centre + (128.0 - (mean(sl) + mean(sh)) / 2) / gslope)) if gslope else centre
        d = int(min(120, max(8, round(12 / abs(gslope))))) if gslope else 120

        def pair(gated: bool, convs: int) -> P.CalProbePoint:
            a, b = measure(zero - d, gated, convs), measure(zero + d, gated, convs)
            cores = tuple(P.CalProbeCore((x.mean + y.mean) / 2, math.sqrt((x.rms ** 2 + y.rms ** 2) / 2),
                                         math.sqrt((x.rms_within ** 2 + y.rms_within ** 2) / 2), min(x.lo, y.lo),
                                         max(x.hi, y.hi), x.clipped + y.clipped)
                          for x, y in zip(a.cores, b.cores))
            return P.CalProbePoint(0, zero, a.cls, convs * 2, (cores[0], cores[1]))

        pts += [sl, sh, pair(True, 8), pair(False, 64), measure(zero + 150, False, 64), measure(zero - 150, False, 64)]
        rc, fp, fcls, fcore, val, within = 0, 0xFF, 0, 0, 0, 0
        forced = getattr(self, "cal_probe_fail", None)
        for i, p in enumerate(pts[2:], 2):
            for k, c in enumerate(p.cores):
                if rc:
                    break
                if c.clipped:
                    rc, val = 5, c.clipped
                # One noise level for both rates (cfg.noise_lsb, 1 LSB by default, on the gated limit): the model checks
                # the full-rate limit for every point. Real units read gated far lower (bench 0.06..0.50 against 1.19..2.75)
                elif c.rms * 16 > P.CAL_NOISE_LIMIT16_FULL:
                    rc, val = 6, int(round(c.rms * 16))
                if rc:
                    fp, fcls, fcore, within = i, p.cls, k, int(round(c.rms_within * 16))
        if forced is not None:
            fp, rc = forced
            fcls, fcore, val, within = pts[fp].cls, 1, int(round(pts[fp].cores[1].rms * 16)), \
                int(round(pts[fp].cores[1].rms_within * 16))
        return P.CalProbeResp(ch, rng, rc, fp, fcls, fcore, flags, val, within, centre, zero, 0, 0,
                              self._cal_points(pts), d)

    @staticmethod
    def _cal_points(pts: list[P.CalProbePoint]) -> list[P.CalProbePoint]:
        empty = P.CalProbeCore(0.0, 0.0, 0.0, 0, 0, 0)
        out = [P.CalProbePoint(i, p.pwm, p.cls, p.conversions, p.cores) for i, p in enumerate(pts)]
        return out + [P.CalProbePoint(i, 0, 0, 0, (empty, empty)) for i in range(len(out), 8)]

    def _cmd_bode_ctrl(self, req: P.Frame) -> P.Frame:
        return self._ok(req, self.bode.ctrl(P.BodeCtrlReq.unpack(req.payload)))

    def _cmd_read_bode(self, req: P.Frame) -> P.Frame:
        r = P.ReadBodeReq.unpack(req.payload)
        return self._ok(req, self.bode.read(r.first, r.count))

    def internal_request(self, frame: P.Frame) -> P.Frame:
        """A request of the simulated firmware's own Bode engine (``_LoopLink``): executed like a host request but
        without touching the duplicate cache, and WAIT blocks here instead of becoming the host's deferred request.
        Raises ``_BodeAbort`` once when the sweep was aborted (BODE_CTRL abort, view closed, a host ARM)."""
        self.bode.check_abort()
        if frame.cmd == P.Cmd.WAIT:
            (tmo,) = struct.unpack_from("<I", frame.payload + bytes(4))
            deadline = time.monotonic() + min(tmo, 10000) / 1000.0
            while True:
                with self.lock:
                    self.tick()
                    self.bode.check_abort()
                    if self.acq_state not in (P.AcqState.ARMED, P.AcqState.TRIGGERED) or \
                            time.monotonic() >= deadline:
                        return self._ok(frame, self.status_bytes())
                time.sleep(0.0005)
        with self.lock:
            saved, self._internal = self.last_resp, True
            try:
                resp = self.handle(frame)
            finally:
                self._internal, self.last_resp = False, saved
        if resp is None:
            raise RuntimeError(f"internal {frame.cmd_name()} became deferred")
        return resp

    def _cmd_get_battery(self, req: P.Frame) -> P.Frame:
        return self._ok(req, P.BatteryResp(18, 0, 41, 0).pack())

    def _cmd_test_pattern(self, req: P.Frame) -> P.Frame:
        r = P.TestPatternReq.unpack(req.payload)
        if r.length > P.MAX_RESPONSE_PAYLOAD:
            raise _DevErr(P.Status.E_BAD_ARG, 0, f"length {r.length} > {P.MAX_RESPONSE_PAYLOAD}")
        if r.pattern > 3:
            raise _DevErr(P.Status.E_BAD_ARG, 4, f"pattern {r.pattern}")
        return self._ok(req, P.test_pattern_bytes(r.length, r.pattern))

    def _channel_arg(self, b: int, detail: int = 0) -> int:
        """detail: byte offset (0) for GET_CHANNEL / GET_CAL, 0xFF for SET_CHANNEL (14.4)."""
        if b > 1:
            raise _DevErr(P.Status.E_BAD_ARG, detail, f"channel {b} > 1")
        return b

    def _cmd_get_channel(self, req: P.Frame) -> P.Frame:
        ch = self._channel_arg(req.payload[0])
        return self._ok(req, self.channel_config(ch).pack())

    def _cmd_set_channel(self, req: P.Frame) -> P.Frame:
        r = P.SetChannelReq.unpack(req.payload)
        ch = self._channel_arg(r.channel, 0xFF)
        cs = self.settings.channels[ch]
        M = P.SetChannelReq
        if r.mask & M.M_RANGE and r.range > 6:
            raise _DevErr(P.Status.E_BAD_ARG, 1, f"range {r.range} > 6")
        if r.mask & M.M_COUPLING and r.coupling > 1:
            raise _DevErr(P.Status.E_BAD_ARG, 2, f"coupling {r.coupling} > 1")
        if r.mask & M.M_PROBE and r.probe_index > 6:
            raise _DevErr(P.Status.E_BAD_ARG, 5, f"probe_index {r.probe_index} > 6")
        if r.mask & M.M_OFFSET and r.offset_mode > 1:
            raise _DevErr(P.Status.E_BAD_ARG, 3, f"offset_mode {r.offset_mode} > 1")
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
                    raise _DevErr(P.Status.E_BAD_ARG, 3, f"offset_pwm {r.offset} not in 0..2000")
                cs.dcoffset = r.offset - center
            else:
                cs.dcoffset = r.offset
        if r.mask & (M.M_OFFSET | M.M_RANGE):
            center = self.cal[ch].dc_calibration_offset[cs.range]
            if not 0 <= center + cs.dcoffset <= 2000:
                raise _DevErr(P.Status.E_BAD_ARG, 3, f"offset PWM {center + cs.dcoffset} not in 0..2000")
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
                raise _DevErr(P.Status.E_BAD_ARG, 4, f"timeperdiv index {r.value} > 34")
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
            if r.source > (P.TRIG_SOURCE_PATTERN if self.caps & P.Caps.PATTERN_GEN else 2):
                raise _DevErr(P.Status.E_BAD_ARG, 0, f"source {r.source}")
            if r.source == P.TRIG_SOURCE_PATTERN and tr.ttype != P.TRIG_EDGE:
                raise _DevErr(P.Status.E_BAD_ARG, 0, "the pattern marker needs the edge type (SET_TRIGGER2 type 0)")
            if r.source == 2:
                self.settings.gen.enable = 0  # fw 0.2.9: selecting EXT switches the generator off
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
            if self.settings.decim & 3 and new.total_samples > P.DECIM_MAX_TOTAL:
                raise _DevErr(P.Status.E_BAD_ARG, 5, "total > 8190 with the filter on")
        if r.mask & M.M_HOLDOFF:
            if r.holdoff and not (self.caps & P.Caps.HOLDOFF):
                raise _DevErr(P.Status.E_UNSUPPORTED, 6, "hold-off not implemented by this FPGA")
            new.holdoff = r.holdoff
            self.holdoff_clear_pending = True  # 1.26: written to 0x1B with the clear request (0x66 = 1)
        if r.mask & M.M_AUTO_TIMEOUT:
            new.auto_timeout = r.auto_timeout
        if r.mask & (M.M_PRETRIGGER | M.M_TOTAL):
            if new.total_samples % 2:
                raise _DevErr(P.Status.E_BAD_ARG, 5, f"total_samples {new.total_samples} must be even")
            if new.pretrigger_samples % 2:
                raise _DevErr(P.Status.E_BAD_ARG, 4, f"pretrigger_samples {new.pretrigger_samples} must be even")
            if not 80 <= new.total_samples <= MAX_TOTAL_SAMPLES:
                raise _DevErr(P.Status.E_BAD_ARG, 5, f"total_samples {new.total_samples} not in 80.."
                                                     f"{MAX_TOTAL_SAMPLES}")
            if new.pretrigger_samples > new.total_samples - 2:
                raise _DevErr(P.Status.E_BAD_ARG, 4, f"pretrigger {new.pretrigger_samples} > total - 2 "
                                                     f"({new.total_samples - 2})")
        self.settings.trigger = new
        return self._ok(req, self.trigger().pack())

    def _cmd_set_trigger2(self, req: P.Frame) -> P.Frame:
        """SET_TRIGGER2 (protocol 1.26, row 66; pc_cmds.c h_set_trigger2): every field written, validated in byte-offset
        order with E_BAD_ARG detail = the offset; nothing written when a field is refused."""
        r = P.Trigger2.unpack(req.payload)
        tr = self.settings.trigger
        new = replace(tr)
        self._check_trigger2(r, engine=False)
        if r.source == 2 and tr.source != 2:
            self.settings.gen.enable = 0  # as SET_TRIGGER: selecting EXT switches the generator off
        new.ttype, new.edge, new.source, new.level = r.type, r.polarity, r.source, r.level
        new.longer = int(bool(r.flags & P.TRIG2_F_LONGER))
        new.n, new.h, new.holdoff = r.n, r.h, r.holdoff
        self.settings.trigger = new
        self.holdoff_clear_pending = True  # 0x66 = 1: a new definition is a new session
        return self._ok(req, self.trigger().pack())

    def counter_level_now(self) -> int:
        """The counter's Schmitt level in effect: the configured code, or with level 0 the trigger level (fw-c7
        pc_glue_counter_pass: scopesettings.triggerlevel clamped to 0..255, taken every main-loop pass)."""
        if self.counter_level != P.COUNTER_LEVEL_FOLLOW:
            return self.counter_level
        return min(255, max(0, int(self.settings.trigger.level)))

    def counter_snapshot(self) -> P.CounterResp:
        """The 0x35 snapshot (protocol 1.26): the latest complete gate of the counter on its source, from the signal's
        periodic edges through the Schmitt thresholds (``signal_edges`` / ``gate_count``), min / max from the noise-free
        extremes plus the expected noise extreme over the gate's 200 MSa/s samples. ``new`` = the gate sequence moved
        since the last read."""
        gate_s = P.COUNTER_GATES[self.counter_gate]
        src = self.counter_source
        if src == 0:
            src = (1, 2, 3)[min(self.settings.trigger.source, 2)]
        flags = (P.COUNTER_F_EXT if src == 3 else 0) | (P.COUNTER_F_CH2 if src == 2 else 0)
        level = self.counter_level_now()
        resp = P.CounterResp(flags, 0, self.counter_gate, self.counter_source, 0, 0, 0, 255, 0, level,
                             self.counter_hyst, 0)
        if gate_s <= 0:
            return resp
        up = self.now() - self.t_boot
        seq = int(up / gate_s)
        resp.seq = seq & 0xFF
        if self.counter_seq_read != seq:  # the model always has a complete gate behind it
            resp.flags |= P.COUNTER_F_NEW
        self.counter_seq_read = seq
        t_start = max(0.0, (seq - 1) * gate_s)
        gen = self.settings.gen
        if src == 3:
            sig = self.cfg.ext_signal or self.cfg.signals[1]
            edges = signal_edges(sig, gen, EXT_THRESHOLD_V, EXT_THRESHOLD_V)
            resp.min, resp.max = (0, 255) if edges is not None else (0, 0)
        else:
            ch = src - 1
            sig = self.cfg.signals[ch]
            cs = self.settings.channels[ch]
            high = self._code_to_volts(ch, level, self.settings)
            low = self._code_to_volts(ch, level - self.counter_hyst, self.settings)
            dc = sig.dc if cs.coupling == 1 else 0.0
            edges = signal_edges(sig, gen, high + dc, low + dc)
            # min / max of the raw codes over the gate: the noise-free extremes of one period (or the level) plus the
            # expected extreme of the noise over the gate's samples
            period = edges[0] if edges is not None else 1e-3
            t = t_start + np.arange(4096) * (period / 4096)
            c = self._analog(ch, t, None, self.settings)
            n_samples = max(2.0, gate_s * P.BASE_CLOCK_HZ)
            ext = self.cfg.noise_lsb * math.sqrt(2 * math.log(n_samples)) if self.cfg.noise_lsb > 0 else 0.0
            resp.min = int(np.clip(np.rint(c.min() - ext), 0, 255))
            resp.max = int(np.clip(np.rint(c.max() + ext), 0, 255))
        if edges is not None:
            e, t, w = gate_count(edges[0], edges[1], edges[2], t_start, gate_s)
            if e >= 1:
                resp.e, resp.t, resp.w = e, t, w
                resp.flags |= P.COUNTER_F_VALID
                resp.freq_mhz = P.counter_freq_mhz(e, t)
        return resp

    def _code_to_volts(self, ch: int, code: float, settings: Settings) -> float:
        """Inverse of ``_analog(core=None)``: the volts at the BNC (after the coupling) a core-mean raw code stands for."""
        cs = settings.channels[ch]
        cal = self.cal[ch]
        vpl = P.uv_per_lsb_q8(cal.input_calibration[cs.range], cs.range) / 256.0 * 1e-6
        g = self.cfg.core_gain[ch]
        gm = (g[0][0] + g[0][1]) / 2
        zero = self._offset_zero(ch, cs.dcoffset) + (self.raw_zero_offset(ch, 0, 0) + self.raw_zero_offset(ch, 1, 0)) / 2
        if self.cfg.v4 is not None:
            g_ref = (g[1][0] + g[1][1]) / 2
            zero += self.cfg.v4[ch][3] * gm / g_ref
        return (code - zero) * vpl / gm

    def _cmd_get_counter(self, req: P.Frame) -> P.Frame:
        r = P.CounterReq.unpack(req.payload)
        if r.what > P.CounterReq.CONFIG:
            raise _DevErr(P.Status.E_BAD_ARG, 0, f"what {r.what}")
        if r.what == P.CounterReq.CONFIG:
            if r.gate >= len(P.COUNTER_GATES):
                raise _DevErr(P.Status.E_BAD_ARG, 1, f"gate {r.gate}")
            if r.source >= len(P.COUNTER_SOURCES):
                raise _DevErr(P.Status.E_BAD_ARG, 2, f"source {r.source}")
            if r.level != P.COUNTER_LEVEL_FOLLOW and r.hysteresis > r.level:  # level 0 follows the trigger level
                raise _DevErr(P.Status.E_BAD_ARG, 4, "hysteresis > level")
            self.counter_gate, self.counter_source = r.gate, r.source
            self.counter_level, self.counter_hyst = r.level, r.hysteresis
            self.counter_seq_read = None
        return self._ok(req, self.counter_snapshot().pack())

    def _cmd_arm(self, req: P.Frame) -> P.Frame:
        if self.acq_state in (P.AcqState.ARMED, P.AcqState.TRIGGERED):
            raise _DevErr(P.Status.E_STATE, 0, "already ARMED; STOP first")
        if self.seq_setting.mode != P.SEQ_OFF and self._seq_fit(self.seq_setting) != P.SEQ_FIT_OK:
            raise _DevErr(P.Status.E_STATE, 3, "sequencer setting no longer fits the geometry (GET_SEQ byte 16)")
        if not self._internal and self.bode.running:
            self.bode.host_armed()  # 1.20: the PC took over the acquisition (reason 5)
        self.arm()
        return self._ok(req, self.status_bytes())

    def _cmd_force(self, req: P.Frame) -> P.Frame:
        if self.acq_state not in (P.AcqState.ARMED, P.AcqState.TRIGGERED):
            raise _DevErr(P.Status.E_STATE, 0, f"state {P.AcqState(self.acq_state).name}, not ARMED")
        self.force()
        return self._ok(req, self.status_bytes())

    def _cmd_stop(self, req: P.Frame) -> P.Frame:
        pc = self._pending_capture
        if pc is not None and pc.get("computing"):
            self.lock.release()  # the model thread stores its result under the lock
            try:
                self.wait_run_model()
            finally:
                self.lock.acquire()
            pc = self._pending_capture
        if self.acq_state in (P.AcqState.ARMED, P.AcqState.TRIGGERED) and pc is not None and "run" in pc:
            # 1.17: STOP ends a run (0x5D = 1, 0x1A = 0): DONE with what it holds, flag "stopped"
            self._complete(self._stop_run(pc))
            if self.shared:
                self.run_state = P.RunState.STOP
            return self._ok(req, self.status_bytes())
        if self.acq_state in (P.AcqState.ARMED, P.AcqState.TRIGGERED):
            self.acq_state = P.AcqState.ABORTED
            self._pending_capture = None
        if self.deferred is not None:
            self._abort_deferred = True
            self.acq_state = P.AcqState.ABORTED
        if self.shared:
            self.run_state = P.RunState.STOP
        return self._ok(req, self.status_bytes())

    def _cmd_set_acq_state(self, req: P.Frame) -> P.Frame:
        r = P.SetAcqStateReq.unpack(req.payload)
        if r.state not in (P.RunState.STOP, P.RunState.RUN):
            raise _DevErr(P.Status.E_BAD_ARG, 0, f"state {r.state}: 0 STOP, 1 RUN")
        self._set_run_state(r.state)
        return self._ok(req, self.status_bytes())

    def _cmd_get_capture_info(self, req: P.Frame) -> P.Frame:
        if self.capture_id == 0:
            raise _DevErr(P.Status.E_STATE, 0, "no capture since boot")
        return self._ok(req, self.capture_info().pack())

    def _check_capture_readable(self, flags: int = 0) -> None:
        """Shared mode: the latest finished capture of any origin is readable unless a host single is
        rewriting the ring."""
        if flags & P.ReadRawReq.ALLOW_NOT_DONE:
            return
        if self.acq_state in (P.AcqState.ARMED, P.AcqState.TRIGGERED):
            raise _DevErr(P.Status.E_STATE, 0, "not done")
        if not self.ring_valid:
            if self.run_state != P.RunState.RUN:
                raise _DevErr(P.Status.E_STATE, 1 if self.capture_id else 0,
                              "ring overwritten" if self.capture_id else "no capture")
            # firmware: hold the request until the UI capture in flight is DONE (at most 1 s)
            if not self.ui_run_capture():
                raise _DevErr(P.Status.E_BUSY, 1, "UI capture running")
        if self.capture_id == 0:
            raise _DevErr(P.Status.E_STATE, 0, "no capture")

    def _cmd_wait(self, req: P.Frame) -> P.Frame | None:
        (tmo,) = struct.unpack_from("<I", req.payload)
        if tmo > 10000:
            raise _DevErr(P.Status.E_BAD_ARG, 0, f"timeout_ms {tmo} > 10000")
        if self.acq_state == P.AcqState.DONE or self.acq_state not in (P.AcqState.ARMED,
                                                                         P.AcqState.TRIGGERED):
            return self._ok(req, self.status_bytes())
        self.deferred = Deferred(req, self.now() + tmo / 1000.0,
                                 lambda: (P.Status.OK, self.status_bytes()))
        return None

    def _resolve_start(self, mode: int, address: int) -> int:
        R = RING_WORDS
        if mode == 0:
            return address % R
        if mode == 1:
            return (self.capture_start_word + address) % R
        if mode == 2:
            return (self.trigger_addr + address) % R
        raise _DevErr(P.Status.E_BAD_ARG, 1, f"addr_mode {mode}")

    def _cmd_read_raw(self, req: P.Frame) -> P.Frame:
        r = P.ReadRawReq.unpack(req.payload)
        R = RING_WORDS
        if not 1 <= r.core_mask <= 15:
            raise _DevErr(P.Status.E_BAD_ARG, 0, f"core_mask 0x{r.core_mask:X}")
        if r.layout > (3 if self.trig26_on else 2):
            raise _DevErr(P.Status.E_BAD_ARG, 2, f"layout {r.layout}")
        if r.layout == P.ReadRawReq.LAYOUT_PEAK and not self.capture_decim.peak:
            raise _DevErr(P.Status.E_BAD_ARG, 2, "layout 3 needs a peak-detect record")
        if r.layout == P.ReadRawReq.LAYOUT_WIDE and self.capture_decim.peak:
            raise _DevErr(P.Status.E_BAD_ARG, 2, "a peak-detect record: READ_RAW layout 3")
        if r.layout >= 1:
            for ch in (0, 1):
                bits = (r.core_mask >> (2 * ch)) & 3
                if bits not in (0, 3):
                    raise _DevErr(P.Status.E_BAD_ARG, 0, "layout 1 needs both cores of each selected channel")
        if r.addr_mode == P.ReadRawReq.ADDR_SEGMENT:
            return self._read_raw_segment(req, r)
        if r.addr_mode in (1, 2) and self._segment_run_latest():
            raise _DevErr(P.Status.E_STATE, 4, "the latest capture is a segment run: READ_RAW addr_mode 3")
        if not 1 <= r.count <= R:
            raise _DevErr(P.Status.E_BAD_ARG, 8, f"count {r.count} not in 1..{R}")
        ncores = bin(r.core_mask).count("1")
        if 16 + r.count * ncores > P.MAX_RESPONSE_PAYLOAD:
            raise _DevErr(P.Status.E_OVERFLOW, 0, f"{16 + r.count * ncores} > {P.MAX_RESPONSE_PAYLOAD}")
        if self.shared:
            self._check_capture_readable(r.flags)
        elif self.acq_state != P.AcqState.DONE and not (r.flags & P.ReadRawReq.ALLOW_NOT_DONE):
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
        elif r.layout == P.ReadRawReq.LAYOUT_PEAK:  # 1.26: count minima (the core A slot) then count maxima (core B)
            clip = [False, False]
            for ch in (0, 1):
                if r.core_mask & (3 << (2 * ch)):
                    blocks.append(data[2 * ch].tobytes() + data[2 * ch + 1].tobytes())
        elif r.layout == 2:  # 1.9: u16 little-endian per word, low byte core A, high byte core B
            clip = [False, False]
            for ch in (0, 1):
                if r.core_mask & (3 << (2 * ch)):
                    il = np.empty(2 * r.count, dtype=np.uint8)
                    il[0::2] = data[2 * ch]
                    il[1::2] = data[2 * ch + 1]
                    blocks.append(il.tobytes())
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
        if r.flags & ~0x07:
            raise _DevErr(P.Status.E_BAD_ARG, 1, "flags")
        origin = 0
        s = self.settings
        if self.shared:
            self._check_capture_readable()
            if self._segment_run_latest():
                raise _DevErr(P.Status.E_STATE, 5, "the latest capture is a segment run (no trace)")
            origin = self.capture_origin  # 1.3: the CaptureOrigin codes
            s = self.capture_settings
        elif self.control == P.Control.LOCAL:
            self._ui_capture_if_local()
            origin = 1
        elif self.acq_state != P.AcqState.DONE:
            raise _DevErr(P.Status.E_STATE, 0, f"state {P.AcqState(self.acq_state).name}, not DONE")
        pre, n = self.capture_samples
        dec = self.capture_decim
        wide = bool(r.flags & P.ReadTraceReq.WIDE) and dec.k > 0
        bps = 2 if wide else 1
        # firmware (pc_cmds.c h_read_trace): start <= samples, count <= samples - start, 0 = to the end
        if r.start > n:
            raise _DevErr(P.Status.E_BAD_ARG, 4, f"start {r.start} > sample_count {n}")
        count = n - r.start if r.count == 0 else r.count
        if count > n - r.start:
            if not self.shared:  # up to fw 0.2.0 (the 1.2 model here): E_BAD_ARG detail 8
                raise _DevErr(P.Status.E_BAD_ARG, 8, "count")
            count = n - r.start  # fw >= 0.2.1: clipped; the header's count says what was sent
        chans = [c for c in (0, 1) if r.ch_mask & (1 << c)]
        if 32 + 16 * len(chans) + count * len(chans) * bps > P.MAX_RESPONSE_PAYLOAD:
            raise _DevErr(P.Status.E_OVERFLOW, 0, "trace too long; lower count")
        metas, datas = [], []
        sw_idx = -1
        for c in chans:
            tr, fl = self.build_trace(c, s)
            if s.channels[c].invert and not (r.flags & P.ReadTraceReq.NO_INVERT):
                tr = (255 - tr.astype(np.int16)).astype(np.uint8)
            cfg = self.channel_config(c, s)
            metas.append(P.TraceChannelMeta(c, cfg.range, cfg.coupling, fl, cfg.uv_per_lsb_q8, cfg.zero_code_q8,
                                            self.cal[c].adc_a_comp, self.cal[c].adc_b_comp).pack())
            if wide:  # as the FPGA stored them: no compensation, no inversion
                datas.append(self.build_trace16(c)[r.start: r.start + count].astype("<u2").tobytes())
            else:
                datas.append(tr[r.start: r.start + count].tobytes())
            if c == s.trigger.source and 20 <= pre and pre + 64 <= n:
                sw_idx = self._sw_trigger(tr, pre, s.trigger)
        if s.trigger.source not in chans:
            sw_idx = -1
        hdr = P.ReadTraceHeader(self.capture_id, n, r.start, count, pre, sw_idx, self.capture_divider,
                                r.ch_mask, origin, dec.status, dec.shift)
        return self._ok(req, hdr.pack() + b"".join(metas) + b"".join(datas))

    def _sw_trigger(self, tr: np.ndarray, centre: int, trig: TriggerState) -> int:
        lvl = trig.level
        lo, hi = max(2, centre - 40), min(len(tr) - 1, centre + 40)
        for i in range(lo, hi, 2):
            p, c = int(tr[i - 2]), int(tr[i])
            if (trig.edge == 0 and p < lvl <= c) or (trig.edge == 1 and p >= lvl > c):
                return i
        return -1

    def _cmd_acquire_avg(self, req: P.Frame) -> P.Frame | None:
        r = P.AcquireAvgReq.unpack(req.payload)
        if self.decim_config().k:  # the simulator's averaging model reads the legacy 8-bit layout only
            raise _DevErr(P.Status.E_STATE, 2, "ACQUIRE_AVG needs the decimation filter off")
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
            raise _DevErr(P.Status.E_BAD_ARG, 8, f"start + count > total_samples {nsamp}")
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
        """SET_GEN (protocol 1.27): the square as the pattern's two entries (contract 7.4: P16 / H16 from mHz / bp
        with the firmware's integer formulas, or raw), committed: a change while the square runs is taken at the next
        step boundary (``pending`` until then). A frequency or duty write makes the square the pin's source again
        (a SET_PATTERN pattern is replaced); the enable alone switches the pin driver and keeps the pattern."""
        r = P.SetGenReq.unpack(req.payload)
        cur = self.settings.gen
        g = replace(cur)
        E = P.Status.E_BAD_ARG
        if r.mask & P.SetGenReq.M_FREQ and r.freq_mode > 1 or r.mask & P.SetGenReq.M_DUTY and r.duty_mode > 1:
            raise _DevErr(E, 0xFE, "freq_mode / duty_mode 0 or 1")
        if r.duty_mode == 0 and r.duty >> 32:
            raise _DevErr(E, 2, "bytes 16..19 are reserved with duty_mode 0")
        if r.mask & (P.SetGenReq.M_FREQ | P.SetGenReq.M_DUTY):
            sq = cur.source == P.GEN_SOURCE_SQUARE
            bp = P.gen_duty_bp(cur.h16, cur.p16) if sq and cur.p16 else 5000
            p16 = cur.p16 if sq and cur.p16 else P.gen_p16(1_000_000)
            if r.mask & P.SetGenReq.M_FREQ:
                if r.freq_mode == 1:
                    p16 = r.freq
                elif not 0 < r.freq <= P.GEN_MAX_MHZ:
                    raise _DevErr(E, 1, f"frequency {r.freq} mHz not in (0, 25 MHz]")
                else:
                    p16 = P.gen_p16(r.freq)
            if r.mask & P.SetGenReq.M_DUTY:
                if r.duty_mode == 1:
                    h16 = r.duty
                    if h16 >= p16:
                        raise _DevErr(E, 2, "H16 >= P16")
                elif r.duty > 10000:
                    raise _DevErr(E, 2, f"duty {r.duty} bp > 10000")
                else:
                    h16 = P.gen_h16(p16, r.duty)
            else:
                h16 = P.gen_h16(p16, bp)
            why = P.gen_check(p16, h16)
            if why:
                raise _DevErr(E, 1, why)
            running = cur.enable and sq and cur.p16
            g.p16, g.h16, g.source, g.pattern = p16, h16, P.GEN_SOURCE_SQUARE, None
            if running:  # the step in progress completes with its old length
                n_h, _, n_l, _ = P.gen_steps(cur.p16, cur.h16)
                self.pat_pending_until = max(self.pat_pending_until, self.now()) + (2 * max(n_h, n_l) + 1) * 5e-9  # taken from step k + 2
            self.pat_start_dev = None
        if r.mask & P.SetGenReq.M_ENABLE:
            g.enable = 1 if r.enable else 0
        if g.enable and not cur.enable and self.settings.trigger.source == P.TRIG_SOURCE_EXT:
            raise _DevErr(P.Status.E_STATE, 0, "EXT trigger uses the pin")
        self.settings.gen = g
        return self._ok(req, self.gen().pack())

    # --- the streaming logger (protocol 1.27, FPGA 0x153D, contract section 6) --------------------------------

    @property
    def logging(self) -> bool:
        lg = self.logger
        if lg is None:
            return False
        self._log_update(self.now())
        return lg["state"] == P.LOG_RUNNING

    def _log_update(self, now: float) -> None:
        """End a log whose duration elapsed (the firmware's log loop ends, acq_state back to IDLE)."""
        lg = self.logger
        if lg is None or lg["state"] != P.LOG_RUNNING:
            return
        if lg["t_end"] is not None and now >= lg["t_end"]:
            self._log_finish(P.LOG_ENDED, lg["t_end"])
        elif now - lg["t_read"] > P.LOG_HOST_TIMEOUT_S:  # the host went away
            self._log_finish(P.LOG_ERROR, lg["t_read"] + P.LOG_HOST_TIMEOUT_S)

    def _log_finish(self, state: int, t: float) -> None:
        lg = self.logger
        assert lg is not None
        lg["state"], lg["t_stop"] = state, t
        if self.acq_state == P.AcqState.LOGGING:
            self.acq_state = P.AcqState.IDLE

    def _log_words(self, now: float) -> int:
        """Words the FPGA has written since the start (frozen at the end)."""
        lg = self.logger
        assert lg is not None
        t = now if lg["state"] == P.LOG_RUNNING else lg["t_stop"]
        w = max(0, int((t - lg["t_start"]) / lg["word_s"] + 1e-6))
        if lg["end_words"] is not None and (lg["state"] == P.LOG_ENDED or w > lg["end_words"]):
            return lg["end_words"]  # the duration is a word count: ceil(ms x 200,000 / word clocks)
        return w

    def _log_resp(self) -> P.LogResp:
        lg = self.logger
        if lg is None:
            return P.LogResp()
        now = self.now()
        self._log_update(now)
        w = self._log_words(now)
        t = now if lg["state"] == P.LOG_RUNNING else lg["t_stop"]
        return P.LogResp(lg["state"], lg["channels"], lg["format"], lg["flags"], lg["divider"], lg["duration_ms"],
                         lg["sample_clocks"], w * lg["wb"], lg["sent"], lg["gaps"], lg["ring_words"] * lg["wb"],
                         min(0xFFFFFFFF, int((t - lg["t_start"]) * 1000 + 1e-6)))

    def _cmd_log_start(self, req: P.Frame) -> P.Frame:
        r = P.LogStartReq.unpack(req.payload)
        why = P.log_check(r.channels, r.format, r.divider)
        if why is not None:
            raise _DevErr(P.Status.E_BAD_ARG, why[0], why[1])
        s = self.settings
        k_req = P.DECIM_PEAK if s.peak else s.decim & 3
        need = {P.LOG_FMT_RAW8: (0,), P.LOG_FMT_HIGH8: (1, 2, 3), P.LOG_FMT_WIDE16: (1, 2, 3),
                P.LOG_FMT_PEAK: (P.DECIM_PEAK,)}[r.format]
        if k_req not in need:
            raise _DevErr(P.Status.E_BAD_ARG, P.LOG_D_FORMAT, f"format {r.format} does not fit the filter setting "
                                                             f"{P.DECIM_FILTERS[k_req]} (SET_DECIM first)")
        dec = P.decim_config(k_req, s.decim & P.DECIM_SET_FIR, r.divider, 2000, P.DECIM_HAS_FIR)
        if r.format != P.LOG_FMT_RAW8 and not dec.k:
            raise _DevErr(P.Status.E_BAD_ARG, P.LOG_D_FORMAT, f"the filter is not active at divider {r.divider}")
        if self.acq_state in (P.AcqState.ARMED, P.AcqState.TRIGGERED):
            raise _DevErr(P.Status.E_STATE, P.LOG_S_ARMED, "a host capture is armed")
        if self.display_mode not in (P.DisplayMode.SCOPE, P.DisplayMode.PHOSPHOR):
            raise _DevErr(P.Status.E_STATE, P.LOG_S_VIEW, "not in the Scope view")
        now = self.now()
        clocks = P.log_sample_clocks(r.format, r.divider)
        word_clocks = clocks * P.log_samples_per_word(r.format)
        wb = P.log_word_bytes(r.channels, r.format)
        settings = s.copy()
        settings.divider = r.divider
        self.logger = {
            "state": P.LOG_RUNNING, "channels": r.channels, "format": r.format, "divider": r.divider,
            "duration_ms": r.duration_ms, "t_start": now, "t_stop": now,
            "t_end": now + r.duration_ms / 1000 if r.duration_ms else None, "sample_clocks": clocks, "t_read": now,
            "end_words": -(-r.duration_ms * 200_000 // word_clocks) if r.duration_ms else None,
            "word_s": clocks * P.log_samples_per_word(r.format) * 5e-9, "wb": wb,
            "ring_words": max(1, self.cfg.log_ring_bytes // wb), "read_word": 0, "seq": 0, "sent": 0, "gaps": 0,
            "flags": 0, "pending": 0, "t0": self.pyrng.uniform(0.0, 10.0), "seed": self.pyrng.randrange(1 << 62),
            "settings": settings, "dec": dec}
        self.acq_state = P.AcqState.LOGGING
        if self.shared:
            self.run_state = P.RunState.STOP
        return self._ok(req, self._log_resp().pack())

    def _cmd_log_read(self, req: P.Frame) -> P.Frame:
        r = P.LogReadReq.unpack(req.payload)
        lg = self.logger
        if not 1 <= r.max_bytes <= P.LOG_READ_MAX:
            raise _DevErr(P.Status.E_BAD_ARG, 0, f"max_bytes {r.max_bytes} not in 1..65536")
        if lg is None:
            raise _DevErr(P.Status.E_STATE, 0, "no log since the boot: LOG_START first")
        now = self.now()
        lg["t_read"] = now
        self._log_update(now)
        running = lg["state"] == P.LOG_RUNNING
        w = self._log_words(now)
        stall0 = lg["t_start"] + 0.1
        if running and self.cfg.log_stall_s and stall0 <= now < stall0 + self.cfg.log_stall_s:
            # the host's reads stall (cfg.log_stall_s): nothing is answered, the DDR ring keeps filling
            return self._ok(req, P.LogChunkHeader(lg["seq"], lg["read_word"] & 0xFFFFFFFF, 0, P.LOG_CHUNK_RUNNING,
                                                  lg["format"]).pack())
        if self.log_lap_next and w > lg["read_word"]:
            # test hook: the MCU fell behind the FPGA ring (armed until a word is unread: a read in the start's
            # clock tick would otherwise flag a lap of no words)
            lg["read_word"] = min(w, lg["read_word"] + self.log_lap_next)
            self.log_lap_next = 0
            lg["gaps"] += 1
            lg["flags"] |= P.LOG_F_LAP
            lg["pending"] |= P.LOG_CHUNK_GAP
        if w - lg["read_word"] > lg["ring_words"]:  # the DDR ring is full: the oldest words are dropped
            lg["read_word"] = w - lg["ring_words"]
            lg["gaps"] += 1
            lg["flags"] |= P.LOG_F_OVERFLOW
            lg["pending"] |= P.LOG_CHUNK_GAP | P.LOG_CHUNK_OVERFLOW
        n = max(0, min(w - lg["read_word"], r.max_bytes // lg["wb"]))
        k0 = lg["read_word"]
        flags = P.LOG_CHUNK_RUNNING if running else 0
        data = b""
        if n:
            data = self._log_data(k0, n)
            flags |= lg["pending"]
            lg["pending"] = 0
            lg["seq"] += 1
            lg["read_word"] += n
            lg["sent"] += len(data)
        if not running and lg["read_word"] >= w:
            flags |= P.LOG_CHUNK_ENDED
        hdr = P.LogChunkHeader(lg["seq"], k0 & 0xFFFFFFFF, len(data), flags, lg["format"])
        return self._ok(req, hdr.pack() + data)

    def _cmd_log_stop(self, req: P.Frame) -> P.Frame:
        """LOG_STOP: idempotent (no log, or an ended one: LOG as it stands)."""
        if self.logger is None:
            return self._ok(req, P.LogResp().pack())
        self._log_update(self.now())
        if self.logger["state"] == P.LOG_RUNNING:
            self._log_finish(P.LOG_STOPPED, self.now())
        return self._ok(req, self._log_resp().pack())

    def _log_data(self, k0: int, n: int) -> bytes:
        """Ring words k0..k0+n-1 of the log as LOG_READ bytes: per word the enabled channels CH1 then CH2; format 0
        the two raw samples in time order (core B, then A), 1 the filter word's high byte, 2 the word (u16 LE), 3 the
        window's min then max. Filtered samples are the noise-free core-mean code at the word's time (less the
        filter latency) plus the window mean's noise, a cheaper model than the capture's CIC (logs are long)."""
        lg = self.logger
        assert lg is not None
        s, dec, fmt, seed, t0 = lg["settings"], lg["dec"], lg["format"], lg["seed"], lg["t0"]
        chans = [c for c in (0, 1) if lg["channels"] >> c & 1]
        cols: list[np.ndarray] = []
        if fmt == P.LOG_FMT_RAW8:
            w = self._words(t0, k0, n, self.dt(lg["divider"]), s, seed)
            for c in chans:
                cols += [w[2 * c + 1], w[2 * c]]  # core B is the earlier sample of the word
            return np.stack(cols, axis=1).astype(np.uint8).tobytes()
        N = dec.n
        t = t0 + np.arange(k0, k0 + n, dtype=np.float64) * N * 5e-9 - DECIM_LATENCY_S
        rng = np.random.default_rng((seed, k0, 7))
        sigma = math.sqrt(self.cfg.noise_lsb ** 2 + 1 / 12)
        if fmt == P.LOG_FMT_PEAK:
            Q = min(N, 8)
            tt = (t[:, None] - (np.arange(Q)[None, ::-1] + 0.5) * N * 5e-9 / Q).ravel()
            for c in chans:
                codes = self._analog(c, tt, None, s) + rng.normal(0.0, sigma, tt.size)
                codes = np.clip(np.rint(codes), 0, 255).reshape(n, Q)
                cols += [codes.min(axis=1).astype(np.uint8), codes.max(axis=1).astype(np.uint8)]
            return np.stack(cols, axis=1).tobytes()
        for c in chans:
            m = np.clip(self._analog(c, t, None, s), 0.0, 255.0) + rng.normal(0.0, sigma / math.sqrt(N), n)
            y = np.minimum(np.floor(np.clip(m, 0.0, 255.0) * dec.gain / (1 << dec.shift)), 65535).astype(np.uint16)
            if fmt == P.LOG_FMT_HIGH8:
                cols.append((y >> 8).astype(np.uint8))
            else:
                cols.append(y.astype("<u2"))
        return np.stack(cols, axis=1).tobytes()

    # --- the pattern generator and its trigger engine (protocol 1.27) ---------------------------------------

    def _cmd_set_pattern(self, req: P.Frame) -> P.Frame:
        """SET_PATTERN 0x0217 (fw-d5's checks, detail = the byte offset); the table, then the start."""
        d = req.payload
        E = P.Status.E_BAD_ARG
        h = P.PatternHeader.unpack(d[:P.PAT_HEADER_BYTES])
        if h.mode > 3:
            raise _DevErr(E, 0, f"mode {h.mode}")
        if h.flags & ~P.PAT_F_VALID & 0xFF or h.start_source == 3 or \
                h.mode == P.PAT_CONTINUOUS and h.start_source == P.PAT_START_TRIGGER:
            # mode 3 runs as soon as 0x6D is written (fpga-d2), so the engine cannot be its only start source
            raise _DevErr(E, 1, f"flags 0x{h.flags:02X}")
        if not 1 <= h.n <= P.PAT_MAX_STEPS:
            raise _DevErr(E, 8, f"steps {h.n} not in 1..{P.PAT_MAX_STEPS}")
        if len(d) != P.PAT_HEADER_BYTES + P.PAT_STEP_BYTES * h.n:
            raise _DevErr(P.Status.E_BAD_LEN, 0, f"{len(d)} bytes for {h.n} steps")
        if h.commit > 1:
            raise _DevErr(E, 9, "commit 0 / 1")
        if h.start > 1 or h.start and (h.mode == P.PAT_OFF or h.start_source == P.PAT_START_TRIGGER):
            raise _DevErr(E, 10, "start 0 / 1, not with mode 0 or the engine as the only start source")
        if d[11]:
            raise _DevErr(E, 11, "byte 11")
        steps = []
        for i in range(h.n):
            o = P.PAT_HEADER_BYTES + P.PAT_STEP_BYTES * i
            s = P.PatternStep.unpack(d[o:o + P.PAT_STEP_BYTES])
            if s.flags & ~(P.PAT_STEP_LEVEL | P.PAT_STEP_MARK):
                raise _DevErr(E, o, f"step {i} flags")
            if d[o + 1]:
                raise _DevErr(E, o + 1, f"step {i} byte 1")
            if s.duration < P.PAT_MIN_CLOCKS:
                raise _DevErr(E, o + 4, f"step {i}: {s.duration} clocks < {P.PAT_MIN_CLOCKS}")
            steps.append(s)
        driven = h.mode != P.PAT_OFF and not h.flags & P.PAT_F_HIZ
        if driven and (self.settings.trigger.source == P.TRIG_SOURCE_EXT or
                       self.ptrig_armed and self.ptrig.source == P.TRIG_SOURCE_EXT):
            raise _DevErr(P.Status.E_STATE, 0, "EXT trigger uses the pin")
        gap = (h.gap_ns + 2) // 5
        g = self.settings.gen
        old = g.pattern if g.pattern_active else None
        running_old = old is not None and self.pat_start_dev is not None and \
            old.state_at(int((self.now() - self.pat_start_dev) * P.PAT_CLOCK_HZ))[0]
        pat = PatternState(h.mode, h.flags, h.repeat or 65536, gap,
                             tuple((s.level, int(s.mark), s.d16) for s in steps))
        self.settings.gen = replace(g, source=P.GEN_SOURCE_PATTERN, pattern=pat, enable=int(driven))
        if h.commit and running_old:
            self.pat_pending_until = self.now() + 2 * max(dd for _, _, dd in old.steps) / 65536 * 5e-9
        if h.mode == P.PAT_OFF:
            self.pat_start_dev = None
        armed = self.acq_state == P.AcqState.ARMED
        if h.start or h.mode == P.PAT_CONTINUOUS:  # mode 3 starts at the write of 0x6D
            if self.pat_start_dev is None or not running_old:
                # an ARM still waiting out the relay settle holds the main loop: the start comes after it
                self.pat_start_dev = max(self.now(), self.arm_time if armed else 0.0)
                self.pat_marker_seen = False
        st0, bursts, st3 = self.pattern_status()
        if (h.start or h.mode == P.PAT_CONTINUOUS) and armed and self._pending_capture is not None and \
                "run" not in self._pending_capture and not self._pending_capture.get("computing"):
            # the host armed first and starts the pattern now: the pending capture sees it from this moment
            self._pending_capture = self.simulate_capture(self.settings, dev_t0=self.arm_time)
        hdr = replace(h, gap_ns=gap * 5)
        return self._ok(req, P.PatternResp(hdr, st0, st3, bursts, P.pattern_burst16(steps), steps).pack())

    def _check_trigger2(self, r: P.Trigger2, engine: bool) -> None:
        """SET_TRIGGER2's checks in byte-offset order (E_BAD_ARG detail = the offset); ``engine``: the pattern trigger
        engine's (SET_PATTERN_TRIGGER: source 0..2 only, no hold-off)."""
        E = P.Status.E_BAD_ARG
        if r.type in (P.TRIG_RUNT, P.TRIG_WINDOW):
            raise _DevErr(E, P.TRIG2_D_TYPE, "runt / window: FPGA 0x153E")
        if r.type > P.TRIG_TIMEOUT:
            raise _DevErr(E, P.TRIG2_D_TYPE, "type 0..3")
        if r.polarity > 1:
            raise _DevErr(E, P.TRIG2_D_POLARITY, f"polarity {r.polarity}")
        if r.flags & (P.TRIG2_F_RANGE | P.TRIG2_F_BOTH_CORES):
            raise _DevErr(E, P.TRIG2_D_FLAGS, "range / both cores: FPGA 0x153E")
        if r.flags & ~P.TRIG2_F_LONGER:
            raise _DevErr(E, P.TRIG2_D_FLAGS, "flags")
        if r.flags & P.TRIG2_F_LONGER and r.type != P.TRIG_PULSE_WIDTH:
            raise _DevErr(E, P.TRIG2_D_FLAGS, "LONGER is for the pulse width type")
        top = P.TRIG_SOURCE_PATTERN if not engine and self.caps & P.Caps.PATTERN_GEN else P.TRIG_SOURCE_EXT
        if r.source > top:
            raise _DevErr(E, P.TRIG2_D_SOURCE, f"source {r.source}")
        if r.source == P.TRIG_SOURCE_PATTERN and r.type != P.TRIG_EDGE:
            raise _DevErr(E, P.TRIG2_D_SOURCE, "the pattern marker is an edge source (type 0)")
        if r.type in (P.TRIG_PULSE_WIDTH, P.TRIG_TIMEOUT) and not 1 <= r.n <= P.TRIG2_MAX_N:
            raise _DevErr(E, P.TRIG2_D_N, f"N {r.n} not in 1..{P.TRIG2_MAX_N}")
        if r.type == P.TRIG_NTH_EDGE and not 1 <= r.n <= P.TRIG2_MAX_NTH:
            raise _DevErr(E, P.TRIG2_D_N, f"Nth edge count {r.n} not in 1..{P.TRIG2_MAX_NTH}")
        if r.type == P.TRIG_EDGE and r.n:
            raise _DevErr(E, P.TRIG2_D_N, "edge: N must be 0")
        if r.type == P.TRIG_NTH_EDGE and r.h > P.TRIG2_MAX_N:
            raise _DevErr(E, P.TRIG2_D_H, f"H {r.h} > {P.TRIG2_MAX_N}")
        if r.type != P.TRIG_NTH_EDGE and r.h:
            raise _DevErr(E, P.TRIG2_D_H, "H is for the Nth edge type")
        if r.level > 255:
            raise _DevErr(E, P.TRIG2_D_LEVEL, f"level {r.level} > 255")
        if r.level2:
            raise _DevErr(E, P.TRIG2_D_LEVEL2, "level2: FPGA 0x153E")
        if engine and r.holdoff:
            raise _DevErr(E, 16, "the pattern trigger engine has no hold-off")
        if engine and r.reserved:
            raise _DevErr(E, 20, "reserved")

    def _cmd_set_pattern_trigger(self, req: P.Frame) -> P.Frame:
        """SET_PATTERN_TRIGGER 0x021A: the engine's configuration (the own engine: GET 0x42 bit 7 = 1)."""
        r = P.PatternTriggerReq.unpack(req.payload)
        self._check_trigger2(r.trigger2, engine=True)
        if r.armed > 1:
            raise _DevErr(P.Status.E_BAD_ARG, 24, "armed 0 / 1")
        if r.rearm > 1:
            raise _DevErr(P.Status.E_BAD_ARG, 25, "rearm 0 / 1")
        g = self.settings.gen
        if r.armed and r.trigger2.source == P.TRIG_SOURCE_EXT and g.enable and g.source == P.GEN_SOURCE_PATTERN:
            raise _DevErr(P.Status.E_STATE, 0, "EXT trigger uses the pin")
        self.ptrig = replace(r.trigger2)
        self.ptrig_armed, self.ptrig_rearm = r.armed, r.rearm
        if r.armed:
            self.ptrig_event = False  # writing armed = 1 restarts the engine
        return self._ok(req, self._ptrig_resp().pack())

    def _ptrig_resp(self) -> P.PatternTriggerResp:
        t = self.ptrig
        st = P.PTRIG_ST_OWN | (P.PTRIG_ST_EVENT if self.ptrig_event else 0) | (t.type & 3) | \
            (P.STATUS34_COUNTING if self.ptrig_armed and t.type != P.TRIG_EDGE else 0) | \
            (P.STATUS34_LONGER if t.flags & P.TRIG2_F_LONGER else 0) | \
            (P.STATUS34_EXT if t.source == P.TRIG_SOURCE_EXT else 0)
        left = max(0, (t.n & 0xFFFF) - 1) if t.type == P.TRIG_NTH_EDGE and self.ptrig_armed else 0
        return P.PatternTriggerResp(replace(t), self.ptrig_armed, self.ptrig_rearm, st, left)

    def _cal_resp(self, ch: int) -> P.CalResp:
        """GET_CAL (1.12, 256 bytes). With cfg.cal_v3 the stored block is what a base calibration of this model
        measures (per class and core, range-independent here), bound to the current PWM centres."""
        c = self.cal[ch]
        a = self.ring[2 * ch].astype(np.int64)
        b = self.ring[2 * ch + 1].astype(np.int64)
        r = P.CalResp(ch, c.dc_calibration_offset, c.input_calibration, c.adc_a_comp, c.adc_b_comp,
                      c.dc_shift_center, c.dc_shift_size, c.dc_shift_value, int(a.mean()), int(b.mean()))
        s = self.settings
        d = s.channels[ch].dcoffset
        cls = self._clock_class(s)
        r.cal_flags = P.CAL_GATED if s.divider else 0  # the 8-bit readout's class: by the divider (acal_correction)
        r.cal_shift = 128 - P.cal_target(d, c.dc_shift_value)
        if not self.cfg.cal_v3:
            r.cal_version, r.comp_a, r.comp_b = 1, c.adc_a_comp, c.adc_b_comp
            r.cal_zero_q8 = P.zero_code_q8(d, c.dc_shift_value)
            r.corr16 = 8 * (c.adc_a_comp + c.adc_b_comp)
            r.off_a, r.off_b = c.adc_a_comp * 65536, c.adc_b_comp * 65536
            return r
        t = self._v3_tables(ch)
        r.cal_version = 3
        r.cal_flags |= P.CAL_LOADED | P.CAL_BOUND | P.CAL_GAINS_MEASURED
        r.pwm, r.zero, r.slope, r.noise = t.pwm, t.zero, t.slope, t.noise
        r.gain, r.gain_range, r.gain_flags, r.gain_lever = t.gain, t.gain_range, t.gain_flags, t.gain_lever
        v = self._v3_corr(ch, s)
        r.comp_a, r.comp_b = v["comp"]
        r.frac_a, r.frac_b = v["frac"]
        r.gain_a, r.gain_b = v["gain"]
        r.off_a, r.off_b = v["off"]
        r.gain16, r.corr16 = v["gain16"], v["corr16"]
        r.cal_zero_q8 = v["t16"] * 16
        if self.cfg.v4 is not None:
            d, e, q, cm = self.cfg.v4[ch]
            r.track_flags = (P.TRACK_RECORD | P.TRACK_E | P.TRACK_Q | P.TRACK_LAST_USABLE | P.TRACK_PROBE_DATA |
                             P.TRACK_PROBE_VALID | (P.TRACK_APPLIES if cls == P.CAL_CLASS_FULL and
                                                    not self.decim_config(s).k else 0))
            r.track_d_q8, r.track_e_q24, r.track_q_q24 = round(d * 256), round(e * 2 ** 24), round(q * 2 ** 24)
            r.track_age_ms, r.track_records, r.track_last_pairs, r.track_spread16 = 40, 512, 2100, 16 * 30
            r.cm_q8, r.cm_probes, r.cm_age_ms = round(cm * 256), 6, 12000
        if self.proto_minor >= 16:
            if self.cal_prov_bad:
                r.prov_state, r.prov = 2, b"APRV" + bytes(52)
            elif self.cal_prov is not None:
                p = self.cal_prov
                r.prov_state, r.prov = 1, P.pack_provenance(p)
                now_min = self.minutes_stored + int(self.now() - self.t_boot) // 60
                r.prov_power_ons_since = self.power_on - p.power_on if self.power_on >= p.power_on else P.CAL_AGE_NONE
                r.prov_minutes_since = now_min - p.minutes if now_min >= p.minutes else P.CAL_AGE_NONE
        return r

    def _cmd_get_cal(self, req: P.Frame) -> P.Frame:
        ch = self._channel_arg(req.payload[0])
        r = self._cal_resp(ch)
        if self.proto_minor < 24:
            return self._ok(req, r.pack()[:356])
        if self.cap_stored is not None:
            r.cap_trailer, (r.cap_stored, r.cap_stored_fpga) = 1, self.cap_stored
        r.cap_flags = self.cap_flags & (P.CAPF_HAS | P.CAPF_TRAINED | P.CAPF_OTHER)
        r.cap_value = self.cap_value
        return self._ok(req, r.pack())

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
        elif self.shared and self.dtr:
            fb[4:24, 700:756] = 0x07E0  # green "MCP" chip (collaborative mode)
        Pw, Tw = self.capture_geometry
        for ch, colour in ((0, 0xFFE0), (1, 0x07FF)):
            a = self.ring[2 * ch, (self.capture_start_word + np.arange(Tw)) % RING_WORDS]
            x = np.linspace(0, len(a) - 1, 800).astype(int)
            y = (479 - a[x].astype(int) * 479 // 255)
            fb[y, np.arange(800)] = colour
        return fb

    # --- SD sector commands (fw 0.2.2, protocol section 14 row 40) ---------------------------------------

    def _cmd_sd_info(self, req: P.Frame) -> P.Frame:
        sd = self.sd
        if not sd.present:
            raise _DevErr(P.Status.E_IO, 0, "no card")
        verify = bool(req.payload[0] & P.SD_FLAG_VERIFY)
        info = P.SdInfo(sd.card_sectors, sd.part_start, sd.write_end, P.SD_STAGE_BYTES,
                        [sd.header(s, verify) for s in (16, 80, 750)])
        if self.proto_minor >= 16:
            if req.payload[0] & P.SD_FLAG_IDENT:
                if self.ident_cache is None:
                    self.ident_cache = self._identify_card_image()
                    info.ident = self.ident_cache
                else:
                    info.ident = P.SdIdent(**{**self.ident_cache.__dict__, "ident_ms": 0})
            else:
                info.ident = self.ident_cache or P.SdIdent()
        return self._ok(req, info.pack())

    def _identify_card_image(self) -> P.SdIdent:
        """Row 55: the identity record in the last 1024 bytes (state 1), else the #VER string (state 3; the sim's
        images have no 0.2.47-0.2.51 build tail), else unknown (4); 5 without an eGON.EXE at 750."""
        head = self.sd.read(750, 1)
        magic, length, _ = P.egon_info(head)
        if magic != 2 or not 0 < length <= (self.sd.limit - 750) * 512:
            return P.SdIdent(state=5, ident_ms=3)
        img = self.sd.read(750, (length + 511) // 512)[:length]
        tail = img[-1024:]
        rec = None
        for off in range(0, len(tail) - 35, 4):
            if struct.unpack_from("<II", tail, off) == P.IDENT_MAGIC:
                rec = P.parse_ident_record(tail[off: off + 36])
        own = (self.cfg.fw_version, SIM_BUILD, SIM_BUILD_TIME)
        if rec is not None:
            running = (rec.version, rec.build, rec.time) == own
            return P.SdIdent(1, P.SD_IDENT_RUNNING if running else 0, rec.proto[0], rec.proto[1],
                             rec.version.encode(), rec.build, rec.time, length, 40)
        i = img.find(b"#OK,FNIRSI-1013D-AI,fw=")
        if i >= 0:
            text = img[i: i + 80].split(b"\0", 1)[0].decode("ascii", "replace")
            fields = dict(x.split("=", 1) for x in text.split(",")[2:] if "=" in x)
            maj, _, mnr = fields.get("proto", "0.0").partition(".")
            return P.SdIdent(3, 0, int(maj or 0), int(mnr or 0), fields.get("fw", "").encode(), 0, (0,) * 6, length,
                             350)
        return P.SdIdent(4, 0, image_length=length, ident_ms=350)

    def _cmd_get_health(self, req: P.Frame) -> P.Frame:
        flags = req.payload[0] if req.payload else 0
        if flags & ~(P.HEALTH_CLEAR_RESTART | P.HEALTH_RESTART_TEST) or \
                (flags & P.HEALTH_RESTART_TEST and not self.caps & P.Caps.DEBUG_RAW):
            raise _DevErr(P.Status.E_BAD_ARG, 0, "flags")
        up = int((self.now() - self.t_boot) * 1000)
        r = P.HealthResp(usb_state=3, usb_flags=0x1B, usb_power=0x50, usb_faddr=5, since_config_ms=up,
                         cp15=0x0005107D, power_on=self.power_on, minutes=self.minutes_stored + up // 60000,
                         uptime_ms=up & 0xFFFFFFFF, life_flags=self.life_flags, last_end=self.last_end,
                         settings_flags=1 if self.settings_changed else 0, settings_eval_ms=400,
                         enc_mode=self.enc_mode, enc_last33=self.enc_last33, enc_flags=self.enc_flags,
                         enc_boot33=P.ENC33_GOOD, enc_bad=self.enc_bad,
                         enc_recovered=self.enc_recovered, enc_checked=self.enc_checked,
                         cap_value=self.cap_value, cap_flags=self.cap_flags, cap_flagged=self.cap_flagged,
                         cap_counts=self.cap_counts, cap_reads=self.cap_reads, cap_flagged_n=self.cap_flagged_n,
                         fw_fpga_version=FPGA_VERSION, fpga_flags=self.fpga_flags)
        if self.proto_minor < 24:  # 1.19-1.23: layout 1, 104 bytes
            r.layout, r.length = 1, 104
        elif self.proto_minor < 25:  # 1.24: layout 2, 124 bytes
            r.layout, r.length = 2, 124
        rec = self.restart_record
        if rec is not None:
            r.rec_flags = 1 | (2 if self.restart_record_this_power_on else 0)
            r.rec_type, r.rec_restarts, r.rec_power_on, r.rec_minutes = (rec["type"], rec["restarts"],
                                                                         rec["power_on"], rec["minutes"])
            for k in ("exc_lr", "spsr", "exc_sp", "reset_lr", "reset_sp", "reset_cpsr", "fsr", "far"):
                setattr(r, "rec_" + k, rec[k])
        out = self._ok(req, r.pack()[:r.length])
        if flags & P.HEALTH_CLEAR_RESTART:
            self.restart_record = None
        if flags & P.HEALTH_RESTART_TEST:
            self.self_restart()
        return out

    def _cmd_save_report(self, req: P.Frame) -> P.Frame:
        if not self.sd.present:
            raise _DevErr(P.Status.E_IO, 0, "no card")
        for nn in range(100):
            name = f"reports/report-{self.power_on:05d}-{nn:02d}.txt"
            if name not in self.reports:
                break
        else:
            raise _DevErr(P.Status.E_IO, 8, "FR_EXIST: 100 reports with this power_on")
        text = (f"FNIRSI 1013D report\nrunning fw {self.cfg.fw_version} build {SIM_BUILD:08x}\n"
                f"power-on {self.power_on}\nserial {self.cfg.serial}\n")
        self.reports[name] = text
        nb = name.encode()
        return self._ok(req, P.SaveReportResp(len(text), len(nb), nb).pack())

    def _cmd_sd_read_sectors(self, req: P.Frame) -> P.Frame:
        r = P.SdReadReq.unpack(req.payload)
        if not 1 <= r.count <= P.SD_MAX_SECTORS:
            raise _DevErr(P.Status.E_BAD_ARG, 4, f"count {r.count} not 1..128")
        if not self.sd.present:
            raise _DevErr(P.Status.E_IO, 0, "no card")
        if r.sector + r.count > self.sd.card_sectors:
            raise _DevErr(P.Status.E_BAD_ARG, 0, "beyond the card")
        data = self.sd.read(r.sector, r.count)
        return self._ok(req, P.SdReadHeader(r.sector, r.count, P.crc32_iso_hdlc(data)).pack() + data)

    def _cmd_sd_stage(self, req: P.Frame) -> P.Frame:
        h = P.SdStageHeader.unpack(req.payload)
        data = req.payload[8:]
        if h.length != len(data) or h.length > P.SD_STAGE_MAX_CHUNK:
            raise _DevErr(P.Status.E_BAD_LEN, 4, f"length {h.length} != payload - 8 ({len(data)})")
        if h.offset + h.length > P.SD_STAGE_BYTES:
            raise _DevErr(P.Status.E_BAD_ARG, 0, "beyond 64 KiB")
        self.sd_stage[h.offset: h.offset + h.length] = data
        return self._ok(req, P.SdStageResp(h.offset, h.length, P.crc32_iso_hdlc(bytes(data))).pack())

    def _cmd_sd_write_sectors(self, req: P.Frame) -> P.Frame:
        r = P.SdWriteReq.unpack(req.payload)
        sd = self.sd
        if r.magic != P.SD_WRITE_MAGIC:
            raise _DevErr(P.Status.E_BAD_ARG, 12, "magic")
        if r.flags & ~(P.SD_WR_CONFIG | P.SD_WR_BOOT):
            raise _DevErr(P.Status.E_BAD_ARG, 6, "flags")
        if not 1 <= r.count <= P.SD_MAX_SECTORS:
            raise _DevErr(P.Status.E_BAD_ARG, 4, "count 1..128")
        if not sd.present:
            raise _DevErr(P.Status.E_IO, 0, "no card")
        if not sd.write_end:
            raise _DevErr(P.Status.E_STATE, 0, "no valid partition table: writes off")
        last = r.sector + r.count - 1
        if r.sector < 16 or last >= sd.card_sectors:
            raise _DevErr(P.Status.E_BAD_ARG, 0, "outside the write window")
        if last >= sd.write_end:  # pc_sd_past_end_text (fw 0.2.101)
            raise _DevErr(P.Status.E_BAD_ARG, 0, f"past sector {sd.limit - 1}, the end of the write window"
                          if sd.part_start >= sd.limit else
                          f"partition starts at {sd.part_start}: it must start at {last + 1} or later")
        if r.sector < 80 and not r.flags & P.SD_WR_BOOT:
            raise _DevErr(P.Status.E_BAD_ARG, 0, "sectors 16-79 need BOOT")
        if r.sector <= 710 and last >= 708 and not r.flags & P.SD_WR_CONFIG:
            raise _DevErr(P.Status.E_BAD_ARG, 0, "sectors 708-710 need CONFIG")
        data = bytes(self.sd_stage[: r.count * 512])
        if P.crc32_iso_hdlc(data) != r.crc32:
            raise _DevErr(P.Status.E_BAD_ARG, 8, "stage crc")
        if last >= 750:
            self.ident_cache = None  # 1.16: the cached card image identity is stale
        if sd.io_error_next:
            err, sd.io_error_next = sd.io_error_next, 0
            raise _DevErr(P.Status.E_IO, (-err) & 0xFFFF, "driver error")
        written = data
        if sd.fail_next_write:
            sd.fail_next_write -= 1
            written = data[:100] + bytes([data[100] ^ 0xFF]) + data[101:]
        sd.put(r.sector, written)
        sd.writes.append((r.sector, r.count))
        back = sd.read(r.sector, r.count)
        if back != data:
            raise _DevErr(P.Status.E_IO, 0xFFFF, "read-back mismatch")
        return self._ok(req, P.SdWriteResp(r.sector, r.count, P.crc32_iso_hdlc(back)).pack())

    def _cmd_debug_fpga(self, req: P.Frame) -> P.Frame:
        try:
            ops = P.decode_debug_ops(req.payload)
        except P.ProtocolError as e:
            raise _DevErr(P.Status.E_BAD_ARG, 0, str(e)[:60]) from e
        out = bytearray()
        R = RING_WORDS
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
            elif op == P.DBG_READ_DATA:
                n = int(arg)  # type: ignore[arg-type]
                c = self._dbg_cmd
                for _ in range(n):
                    out.append(self._dbg_read_byte(c))
        return self._ok(req, bytes(out))

    def _dbg_read_byte(self, c: int) -> int:
        R = RING_WORDS
        if c == 0x06:
            v = self.cfg.fpga_version
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
            self.last_frame_at = self.now()  # 14.21
            cmd = line.strip().upper()
            if cmd == "#VER":
                return (f"#OK,FNIRSI-1013D-AI,fw={self.cfg.fw_version},proto={P.PROTO_MAJOR}.{self.proto_minor},"
                        f"fpga=0x{self.cfg.fpga_version:04X}")
            if cmd == "#LOCAL":
                self.go_local(P.ExitReason.HOST_RELEASE)
                return "#OK"
            if cmd == "#RESET":
                self.reset_link()
                return "#OK"
            return "#ERR,UNKNOWN"

    def reset_link(self) -> None:
        """#RESET (protocol 14.25): drop a pending WAIT without an answer and the duplicate cache; REMOTE
        ownership and acquisition state stay."""
        with self.lock:
            if self.deferred is not None or self.last_resp is not None:
                self.log_event(13)
            self.link["link_resets"] += 1
            self.deferred = None
            self._abort_deferred = False
            self.last_resp = None

    def link_down(self) -> None:
        """DTR 1->0 / TCP close."""
        with self.lock:
            self.log_event(9, 0)
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
        #: test hook: treat the next n valid requests as corrupted (answer E_BAD_CRC, do not execute)
        self.bad_crc_next = 0
        #: test hook: lose the next n valid requests (never executed, never answered; the firmware RX-loss bug)
        self.lose_next = 0
        #: if set, ``lose_next`` only applies to requests with this cmd
        self.lose_cmd: int | None = None
        #: test hook: {cmd: n} drop the USB link (close the connection) when the n-th request of cmd arrives,
        #: without executing it (bench 2026-10-18: the scope fell off USB during --persist)
        self.drop_link_on: dict[int, int] = {}
        self._drop_link = False

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
                    self.device.link["dtr_up"] += 1
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
                self.device.link["rx_packets"] += (len(data) + 511) // 512
                self.device.link["rx_bytes"] += len(data)
                for chunk, is_reset in _split_reset(data):
                    if is_reset:
                        parser.reset()
                        self.device.reset_link()
                        try:
                            client.sendall(b"#OK\n")
                        except OSError:
                            pass
                        continue
                    for ev in parser.feed(chunk):
                        self._event(client, ev)
                if self.device.fel_requested or self._drop_link:
                    log.info("closing the link (%s)", "ENTER_FEL" if self.device.fel_requested else "drop hook")
                    self.device.fel_requested = False
                    self._drop_link = False
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
        self.device.link["dtr_down"] += 1
        self.device.link_down()
        log.info("host disconnected")

    def _event(self, client: socket.socket, ev: P.ParseEvent) -> None:
        dev = self.device
        L = dev.link
        if isinstance(ev, P.Garbage):
            L["junk_bytes"] += len(ev.data)
        elif isinstance(ev, P.BadHeader):
            L["hdr_crc_errors"] += 1
        if isinstance(ev, P.Frame):
            if ev.is_response:
                L["responses_dropped"] += 1
                return
            if ev.proto_major != P.PROTO_MAJOR:
                L["version_errors"] += 1
            if self._drop_link:
                return
            if ev.cmd in self.drop_link_on:
                self.drop_link_on[ev.cmd] -= 1
                if self.drop_link_on[ev.cmd] <= 0:
                    del self.drop_link_on[ev.cmd]
                    self._drop_link = True
                    return
            if self.lose_next and self.lose_cmd in (None, ev.cmd):
                self.lose_next -= 1
                return
            if self.bad_crc_next:
                self.bad_crc_next -= 1
                self._send(client, P.response(ev, P.error_payload(0, "payload crc"), P.Status.E_BAD_CRC))
                return
            resp = dev.handle(ev)
            if resp is not None:
                self._send(client, resp)
        elif isinstance(ev, P.BadPayloadCrc):
            L["payload_crc_errors"] += 1
            with dev.lock:
                dev.last_frame_at = dev.now()
            self._send(client, P.response(ev.frame, P.error_payload(0, "payload crc"), P.Status.E_BAD_CRC))
        elif isinstance(ev, P.Oversize):
            L["oversize"] += 1
            self._send(client, P.response(ev.frame, P.error_payload(0, f"frame over {P.MAX_REQUEST_FRAME}"),
                                          P.Status.E_BAD_LEN))
        elif isinstance(ev, (P.AsciiLine, P.AsciiOverflow)):
            reply = dev.handle_ascii(ev.text) if isinstance(ev, P.AsciiLine) else "#ERR,UNKNOWN"
            try:
                client.sendall(reply.encode() + b"\n")
            except OSError:
                pass

    def _send(self, client: socket.socket, resp: P.Frame) -> None:
        lim = self.device.cfg.fail_responses_over
        if lim is not None and len(resp.payload) > lim:
            log.info("dropping %d-byte response (fail_responses_over=%d)", len(resp.payload), lim)
            return
        if self.drop_next:
            self.drop_next -= 1
            return
        data = bytearray(resp.encode())
        L = self.device.link
        L["responses"] += 1
        L["tx_packets"] += (len(data) + 511) // 512
        if len(data) % 512 == 0:
            L["tx_zlps"] += 1
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


def _split_reset(data: bytes) -> list[tuple[bytes, bool]]:
    """Split raw input around ``#RESET`` + CR/LF lines, which the device honours even inside a partial frame."""
    out: list[tuple[bytes, bool]] = []
    while True:
        i = data.find(b"#RESET")
        if i < 0 or i + 6 >= len(data) or data[i + 6] not in (0x0A, 0x0D):
            out.append((data, False))
            return out
        out.append((data[:i], False))
        out.append((b"", True))
        data = data[i + 7:]


# ---------------------------------------------------------------------------------------------------------
# The on-device Bode plot (protocol 1.20, docs/protocol.md row 60)


class _BodeAbort(Exception):
    def __init__(self, reason: int):
        super().__init__(f"bode sweep aborted (reason {reason})")
        self.reason = reason


class _BodeStop(Exception):
    pass


class _BodeReadoutFault(Exception):
    """READ_BODE reason 8 (``SimConfig.bode_readout_fault``)."""


class _LoopLink:
    """In-process link from the simulated firmware's Bode engine to its own command handlers."""

    def __init__(self, dev: "Device"):
        self.dev = dev
        self.parser = P.FrameParser(max_payload=P.MAX_REQUEST_PAYLOAD)
        self.out = bytearray()

    @property
    def description(self) -> str:
        return "sim-internal"

    def write(self, data: bytes) -> None:
        for ev in self.parser.feed(data):
            if isinstance(ev, P.Frame):
                self.out += self.dev.internal_request(ev).encode()

    def read(self, max_bytes: int, timeout: float) -> bytes:
        if not self.out:
            time.sleep(min(timeout, 0.001))
            return b""
        b = bytes(self.out[:max_bytes])
        del self.out[:max_bytes]
        return b

    def flush_input(self) -> None:
        self.out.clear()

    def close(self) -> None:
        pass


#: device speed-up over the host tool's estimate (no USB round trip per point; fw-bode, row 60: about 2-3 x)
BODE_DEVICE_SPEEDUP = 2.5


class SimBode:
    """The firmware's Bode view and sweep engine (protocol 1.20) for the simulator. A sweep is the host tool's
    ``BodeRunner`` driving this device through ``_LoopLink`` (bode_sweep.c is a port of it: the same plan, ranging,
    splice and flags), in a thread; READ_BODE packs its results into rows. The view saves the settings when it opens and
    gives them back when it closes (a sweep leaves its own settings, as on the scope)."""

    def __init__(self, dev: "Device"):
        self.dev = dev
        self.shown = False
        self.repeat = False
        self.sweep_id = 0
        self.state = 0
        self.reason = 0
        self.thru_sweep = False
        self.exhausted = 0
        #: settings in effect (the screen's, or the host's with BODE_CTRL flags bit 0)
        self.start_hz, self.stop_hz, self.points, self.settle, self.ch2_mode = 100.0, 1e6, 50, 1, 0
        #: the latest sweep's (settle, ch2_mode, start, stop, points); READ_BODE's header shows them once a sweep ran,
        #: and the settings in effect before the first one (fw 0.2.71)
        self.swept: tuple[int, int, float, float, int] | None = None
        self.background = True
        self.plans: list = []
        self.results: dict[int, Any] = {}
        self.current = -1
        self.switches = 0
        self.t_start = 0.0
        self.t_end: float | None = None
        self.thru: Any = None
        self.thru_key: tuple | None = None
        self.thru_applied = False
        self.stop_req = False
        self.abort_reason = 0
        self.abort_raised = False
        self.thread: threading.Thread | None = None
        self.saved: tuple | None = None
        self.prev_display = P.DisplayMode.SCOPE

    @property
    def running(self) -> bool:
        return self.state == 1

    def key(self) -> tuple:
        return (round(min(self.start_hz, self.stop_hz), 2), round(max(self.start_hz, self.stop_hz), 2), self.points)

    def check_abort(self) -> None:
        if self.abort_reason and not self.abort_raised:
            self.abort_raised = True
            raise _BodeAbort(self.abort_reason)

    def host_armed(self) -> None:
        """A host ARM (not the engine's) ends the sweep with reason 5; the engine's capture in flight is dropped."""
        d = self.dev
        self.abort_reason, self.abort_raised = 5, False
        if d.acq_state in (P.AcqState.ARMED, P.AcqState.TRIGGERED):
            d.acq_state = P.AcqState.ABORTED
            d._pending_capture = None

    def estimate_ms(self) -> int:
        from . import bode as BD

        try:
            plans = BD.plan_sweep(self.start_hz, self.stop_hz, self.points)
        except ValueError:
            return 0
        est = BD.estimate_duration(plans, BD.SETTLE_PRESETS[P.BODE_SETTLES[self.settle]], self.background,
                                   self.ch2_mode == 0)
        return int(est / BODE_DEVICE_SPEEDUP * 1000)

    # --- the view -------------------------------------------------------------------------------------------

    def open(self) -> None:
        d = self.dev
        if self.shown:
            return
        self.shown = True
        self.saved = (d.settings.copy(), d.run_state)
        self.prev_display = d.display_mode
        d.display_mode = P.DisplayMode.BODE

    def close(self) -> None:
        d = self.dev
        self._end_thread(6)
        if not self.shown:
            return
        self.shown = False
        self.repeat = False
        if self.saved is not None:
            d.settings = self.saved[0].copy()
            d._set_run_state(self.saved[1])
        d.display_mode = self.prev_display if self.prev_display != P.DisplayMode.BODE else P.DisplayMode.SCOPE

    def _end_thread(self, reason: int) -> None:
        th = self.thread
        if th is None or not th.is_alive():
            return
        self.abort_reason, self.abort_raised = reason, False
        self.repeat = False
        if th is not threading.current_thread():
            d = self.dev
            d.lock.release()  # the engine thread takes the lock for every request
            try:
                th.join(30)
            finally:
                d.lock.acquire()

    # --- BODE_CTRL / READ_BODE ------------------------------------------------------------------------------

    def ctrl(self, r: P.BodeCtrlReq) -> bytes:
        if r.action > P.BODE_CLOSE:
            raise _DevErr(P.Status.E_BAD_ARG, 0, "action 0..10")
        if r.flags & P.BODE_F_SETTINGS:
            a, b = r.start_chz * 10, r.stop_chz * 10  # mHz
            if r.settle >= 4:
                raise _DevErr(P.Status.E_BAD_ARG, 1, "settle 0..3")
            if r.ch2_mode > 1:
                raise _DevErr(P.Status.E_BAD_ARG, 2, "CH2 range mode 0..1")
            if not (1000 <= a <= 10_000_000_000 and 1000 <= b <= 10_000_000_000):
                raise _DevErr(P.Status.E_BAD_ARG, 3, "start / stop 1 Hz .. 10 MHz")
            if not 1 <= r.points <= 200:
                raise _DevErr(P.Status.E_BAD_ARG, 4, "points 1..200")
            self.start_hz, self.stop_hz, self.points = r.start_chz / 100.0, r.stop_chz / 100.0, r.points
            self.settle, self.ch2_mode = r.settle, r.ch2_mode
            self.background = not r.flags & P.BODE_F_NO_BACKGROUND
        act = r.action
        if act in (P.BODE_RUN, P.BODE_REPEAT, P.BODE_THRU_MEASURE, P.BODE_OPEN):
            self.open()
        if act in (P.BODE_RUN, P.BODE_REPEAT, P.BODE_THRU_MEASURE):
            if self.running:
                raise _DevErr(P.Status.E_STATE, 2, "a sweep is running")
            self.repeat = act == P.BODE_REPEAT
            self._start(act == P.BODE_THRU_MEASURE)
        elif act == P.BODE_STOP:
            self.repeat = False
            self.stop_req = True
        elif act == P.BODE_ABORT:
            self._end_thread(6)
        elif act in (P.BODE_THRU_ON, P.BODE_THRU_OFF):
            if self.thru is None:
                raise _DevErr(P.Status.E_STATE, 4, "no thru calibration")
            self.thru_applied = act == P.BODE_THRU_ON
        elif act == P.BODE_THRU_CLEAR:
            self.thru, self.thru_key, self.thru_applied = None, None, False
        elif act == P.BODE_CLOSE:
            self.close()
        return self.header() + struct.pack("<II", self.estimate_ms(), 0)

    def header(self) -> bytes:
        thru = 0
        if self.running and self.thru_sweep:
            thru = 1
        elif self.thru is not None:
            thru = 2 if self.thru_key == self.key() else 3
        if self.t_start:
            elapsed = ((self.t_end if self.t_end is not None else time.monotonic()) - self.t_start) * 1000
        else:
            elapsed = 0
        view = (1 if self.shown else 0) | (2 if self.repeat else 0) | (4 if self.thru_sweep else 0) | \
            ((self.exhausted & 3) << 3)
        settle, ch2_mode, start, stop, points = self.swept if self.swept is not None else \
            (self.settle, self.ch2_mode, self.start_hz, self.stop_hz, self.points)
        return P.BodeHeader(self.sweep_id, self.state, self.reason, view, thru | (0x80 if self.thru_applied else 0),
                            len(self.plans), len(self.results), self.current if self.running else -1, settle,
                            ch2_mode, int(round(start * 100)), int(round(stop * 100)), points, self.switches,
                            int(elapsed)).pack()

    def read(self, first: int, count: int) -> bytes:
        if count == 0 or count > P.BODE_MAX_ROWS:
            count = P.BODE_MAX_ROWS
        out = bytearray(self.header())
        for i in range(first, min(len(self.plans), first + count)):
            out += self._row(i)
        return bytes(out)

    def _row(self, i: int) -> bytes:
        from . import bode as BD

        pp = self.plans[i]
        f_chz = int(round(pp.f_actual * 100))
        pr = self.results.get(pp.period)
        if pr is None:
            return P.BodeRow(pp.period, f_chz, flags=P.BODE_ROW_NOT_MEASURED).pack()
        if self.thru_applied and self.thru is not None and not self.thru_sweep:
            pr = replace(pr, flags=list(pr.flags))
            self.thru.apply(pr)

        def centi(x: float) -> int:
            return max(-32768, min(32767, int(round(x * 100)))) if math.isfinite(x) else -32768

        ph = centi(pr.phase_deg)
        return P.BodeRow(pp.period, f_chz, centi(pr.mag_db), 18000 if ph == -18000 else ph,
                         centi(BD.db(abs(pr.x1) / math.sqrt(2))), centi(BD.db(abs(pr.x2) / math.sqrt(2))),
                         pr.ch1_range, pr.ch2_range, P.bode_flags_from_names(pr.flags, pr.retries > 0)).pack()

    # --- the engine -----------------------------------------------------------------------------------------

    def _start(self, thru_measure: bool) -> None:
        from . import bode as BD

        try:
            plans = BD.plan_sweep(self.start_hz, self.stop_hz, self.points)
        except ValueError:
            self.state, self.reason = 4, 4
            raise _DevErr(P.Status.E_BAD_ARG, 3, "no frequency in the range") from None
        self.sweep_id += 1
        self.swept = (self.settle, self.ch2_mode, self.start_hz, self.stop_hz, self.points)
        self.plans, self.results = plans, {}
        self.state, self.reason, self.current, self.switches, self.exhausted = 1, 0, 0, 0, 0
        self.thru_sweep = thru_measure
        self.stop_req, self.abort_reason, self.abort_raised = False, 0, False
        self.t_start, self.t_end = time.monotonic(), None
        self.thread = threading.Thread(target=self._work, args=(self.sweep_id, thru_measure), name="sim-bode",
                                       daemon=True)
        self.thread.start()

    def _work(self, sid: int, thru_measure: bool) -> None:
        from . import bode as BD
        from .scope import Scope
        from .transport import Transport

        eng, d = self, self.dev
        state, reason = 2, 0
        runner = None
        try:
            sc = Scope(Transport(_LoopLink(d), resync=False))
            sc.t._seq = 0x8000  # far from the host's sequence numbers
            sc.identify()

            class Runner(BD.BodeRunner):
                def _restore(self, sv: Any) -> list[str]:
                    return []  # the view gives the settings back when it closes

                def _result(self, pp: Any, ms: Any, retries: int) -> Any:
                    pr = super()._result(pp, ms, retries)
                    eng.results[pp.period] = pr
                    return pr

                def _prescan(self, pp: Any) -> None:
                    super()._prescan(pp)
                    if d.cfg.bode_readout_fault:
                        raise _BodeReadoutFault()

            def progress(i: int, n: int, text: str) -> None:
                eng.current = i
                eng.switches = runner.switches if runner is not None else 0
                if eng.stop_req:
                    raise _BodeStop()

            scale = d.cfg.time_scale
            runner = Runner(sc, settle=P.BODE_SETTLES[self.settle], ch2_range=P.BODE_CH2_MODES[self.ch2_mode],
                            background=self.background, busy_retry_s=0.0, progress=progress,
                            sleep=(lambda x: time.sleep(x * scale)) if scale > 0 else (lambda x: None))
            res = runner.run(self.plans, None, "thru" if thru_measure else "measure")
            if res.stopped_early:
                state, reason = 4, 2
        except _BodeStop:
            state, reason = 3, 6
        except _BodeReadoutFault:
            state, reason = 4, 8
        except _BodeAbort as e:
            state, reason = (4, 5) if e.reason == 5 else (3, 6)
        except BD.BodeError as e:
            state, reason = 4, 3 if "saturated" in str(e) else 2
        except Exception:  # noqa: BLE001 - the engine reports an error state, the simulator keeps running
            log.exception("sim bode engine")
            state, reason = 4, 2
        with d.lock:
            if self.sweep_id != sid:
                return
            if runner is not None:
                self.switches = runner.switches
                self.exhausted = sum(1 << c for c in runner._offset_exhausted)
            if thru_measure and state == 2:
                pts = [self.results[pp.period] for pp in self.plans if pp.period in self.results]
                self.thru = BD.ThruCal.from_sweep(BD.SweepResult(pts, 0.0, "auto"), "device", (1.0, 1.0))
                self.thru_key, self.thru_applied = self.key(), True
            self.state, self.reason, self.t_end, self.current = state, reason, time.monotonic(), -1
            again = self.repeat and state == 2 and not thru_measure
        if again:
            with d.lock:
                if self.repeat and self.shown and not self.running:
                    self._start(False)


def start_simulator(cfg: SimConfig | None = None, host: str = "127.0.0.1", port: int = 0) -> SimServer:
    """Start an in-process simulator on an ephemeral port (for tests and quick experiments)."""
    return SimServer(Device(cfg or SimConfig()), host, port).start()


# ---------------------------------------------------------------------------------------------------------
# CLI


def build_config(args: argparse.Namespace) -> SimConfig:
    ch1 = parse_signal(args.ch1 or args.signal)
    ch2 = parse_signal(args.ch2) if args.ch2 else companion_signal(ch1)
    return SimConfig(variant=args.variant, fpga_version=args.fpga_version, signals=(ch1, ch2), noise_lsb=args.noise,
                     hw_read_lag=args.hw_read_lag, fw_read_lag=args.fw_read_lag, true_order=args.true_order,
                     trigger_align_offset=args.trigger_align_offset, time_scale=args.time_scale,
                     seed=args.seed, fail_responses_over=args.fail_responses_over, caps_override=args.caps,
                     legacy=args.legacy, ui_capture_period_s=args.ui_period,
                     ext_signal=parse_signal(args.ext) if args.ext else None,
                     log_ring_bytes=args.log_ring, log_stall_s=args.log_stall)


def make_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(prog="fnirsi1013d-sim", description="FNIRSI 1013D protocol v1 device simulator")
    ap.add_argument("--variant", choices=VARIANT_NAMES, default="al3",
                    help="the simulated scope (one since fw 0.2.102: an AL3A10 running the firmware's bitstream)")
    ap.add_argument("--fpga-version", type=lambda x: int(x, 0), default=FPGA_VERSION, metavar="WORD",
                    help=f"the version word the FPGA reports (default 0x{FPGA_VERSION:04X}); another word, e.g. "
                         "0x1532, simulates a scope whose bitstream was not flashed for this firmware: GET_INFO / "
                         "GET_HEALTH flag the mismatch and everything but the basic commands answers E_UNSUPPORTED")
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
    ap.add_argument("--caps", type=lambda x: int(x, 0), metavar="MASK",
                    help="capability mask override, e.g. 0x1803 = firmware 0.1.0 on AL3 (no device averaging, "
                         "screenshot, FORCE, DEBUG_FPGA); default: everything the simulator implements")
    ap.add_argument("--ext", help="signal on the calibration pin for the EXT trigger, default CH2's")
    ap.add_argument("--legacy", action="store_true",
                    help="firmware without collaborative mode (protocol 1.2, exclusive LOCAL/REMOTE)")
    ap.add_argument("--ui-period", type=parse_quantity, default=0.05, metavar="S",
                    help="UI capture period in RUN / LOCAL (default 50ms; 0 = only on demand)")
    ap.add_argument("--fail-responses-over", type=int, metavar="BYTES",
                    help="never send responses with a larger payload (reproduces the 5 KiB firmware bug)")
    ap.add_argument("--log-ring", type=int, default=8 << 20, metavar="BYTES",
                    help="the logger's DDR ring (default 8 MiB); small values make --log-stall overflow it")
    ap.add_argument("--log-stall", type=parse_quantity, default=0.0, metavar="S",
                    help="the logger's host reads stall for S seconds from 0.1 s after LOG_START (tests the DDR "
                         "overflow: the next chunk carries the gap)")
    ap.add_argument("-v", "--verbose", action="store_true")
    return ap


def start_from_args(arg_string: str, port: int = 0) -> SimServer:
    """Start an in-process simulator from a CLI-style string, e.g. ``"--variant al3 --signal decay:tau=80us"``
    (used by ``fnirsi1013d-mcp --sim``)."""
    import shlex

    args = make_parser().parse_args(shlex.split(arg_string, posix=True))
    return SimServer(Device(build_config(args)), args.host, port or 0).start()


def main(argv: list[str] | None = None) -> None:
    args = make_parser().parse_args(argv)
    logging.basicConfig(level=logging.INFO if args.verbose else logging.WARNING,
                        format="%(asctime)s %(name)s %(message)s")
    cfg = build_config(args)
    srv = SimServer(Device(cfg), args.host, args.port)
    mismatch = f", a mismatch: the firmware needs 0x{FPGA_VERSION:04X}" if cfg.fpga_version != FPGA_VERSION else ""
    print(f"fnirsi1013d-sim: FPGA 0x{cfg.fpga_version:04X}{mismatch} on {srv.url}; "
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
