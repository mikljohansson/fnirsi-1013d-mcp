"""MCP server for the FNIRSI 1013D (docs/mcp-tools.md).

Run against the simulator:

    fnirsi1013d-sim --variant al3 --port 5555 --signal decay:tau=80us &
    fnirsi1013d-mcp --port tcp://localhost:5555

Tools are named ``scope_<action>`` (mcp-builder convention: service prefix, snake_case). The mapping to the
names in docs/mcp-tools.md is 1:1 with the prefix added where it was missing (``set_channel`` ->
``scope_set_channel`` ...).

Safety tiers (docs/mcp-tools.md section 2): 0 read-only, 1 acquisition, 2 front-end relays (rate-limited),
3 output on the calibration pin (confirmation token), 4 raw FPGA / FEL (disabled unless
FNIRSI_MCP_ENABLE_RAW=1, confirmation token).
"""

from __future__ import annotations

import argparse
import collections
import datetime as _dt
import hashlib
import json
import logging
import math
import os
import secrets
import struct
import sys
import threading
import time
import zlib
from dataclasses import dataclass, field
from pathlib import Path
from typing import Annotated, Any, Callable, Literal, TypeVar

import anyio
import numpy as np
from mcp.server.fastmcp import Context, FastMCP, Image
from mcp.server.fastmcp.exceptions import ToolError
from mcp.types import ToolAnnotations
from pydantic import Field

from . import analysis as A
from . import bode as BD
from . import protocol as P
from .paths import user_home
from .scope import (CHANNEL_NAMES, SCALING_CAVEAT, Capture, CaptureContext, Scope, ScopeError, SeqError,
                    Spectrum, channel_index, settings_diff)
from .transport import DeviceError, TransportError
from .calibration import UnitCalibration, describe as describe_cal
from .units import fmt_si, parse_quantity

log = logging.getLogger("fnirsi1013d.mcp")

T = TypeVar("T")

# ---------------------------------------------------------------------------------------------------------
# Configuration


@dataclass
class ServerConfig:
    port: str = "auto"
    max_tier: int = 3
    enable_raw: bool = False
    gen_max_hz: float = 1_000_000.0
    gen_max_duty: float = 50.0
    #: .npz output, per-unit calibration, Bode thru calibrations (default <home>/data, paths.user_home())
    data_dir: Path = field(default_factory=lambda: user_home() / "data")
    audit_log: Path | None = field(default_factory=lambda: user_home() / "logs" / "audit.jsonl")
    watchdog_s: int = 15
    relay_changes_per_s: int = 5
    token_ttl_s: float = 120.0
    keepalive_s: float = 5.0
    #: legacy firmware (no SHARED_MODE): release REMOTE, keeping the host settings, after this many idle seconds
    #: (0 = hold it until the server exits)
    legacy_idle_release_s: float = 60.0
    #: close the serial port (or tcp:// link) after this many seconds without a tool running, so other programs
    #: (bench scripts, fnirsi1013d-flash) can open it; the next tool call reconnects (0 = hold it until exit)
    idle_close_s: float = 30.0
    #: collaborative firmware: restarts of a long run after settings changes at the scope before giving up
    max_restarts: int = 5
    #: collaborative firmware reports ui_busy 2 (STARTING) until its first main-loop pass; wait this long
    starting_wait_s: float = 10.0
    inline_max_points: int = 2000
    #: cap on one response payload; larger reads are split (None = the device's max_response_payload)
    max_response: int | None = None
    #: per-unit voltage calibration file; None: <data_dir>/cal/<serial>.json (loaded if present)
    cal_file: Path | None = None

    @classmethod
    def from_env(cls, home: Path | None = None, **override: Any) -> "ServerConfig":
        """Defaults, then ``home`` (``--home``; else FNIRSI_MCP_HOME / ~/.fnirsi1013d), then the FNIRSI_MCP_*
        variables, then the non-None ``override`` values (command-line arguments)."""
        e = os.environ
        cfg = cls()
        if home is not None:
            cfg.data_dir = Path(home) / "data"
            cfg.audit_log = Path(home) / "logs" / "audit.jsonl"
        cfg.port = e.get("FNIRSI_MCP_PORT", cfg.port)
        cfg.max_tier = int(e.get("FNIRSI_MCP_MAX_TIER", cfg.max_tier))
        cfg.enable_raw = e.get("FNIRSI_MCP_ENABLE_RAW", "0") not in ("0", "", "false", "False")
        cfg.gen_max_hz = float(e.get("FNIRSI_MCP_GEN_MAX_HZ", cfg.gen_max_hz))
        cfg.gen_max_duty = float(e.get("FNIRSI_MCP_GEN_MAX_DUTY", cfg.gen_max_duty))
        if e.get("FNIRSI_MCP_MAX_RESPONSE"):
            cfg.max_response = int(e["FNIRSI_MCP_MAX_RESPONSE"])
        if "FNIRSI_MCP_DATA_DIR" in e:
            cfg.data_dir = Path(e["FNIRSI_MCP_DATA_DIR"])
        if e.get("FNIRSI_MCP_CAL_FILE"):
            cfg.cal_file = Path(e["FNIRSI_MCP_CAL_FILE"])
        if e.get("FNIRSI_MCP_LEGACY_IDLE_RELEASE_S"):
            cfg.legacy_idle_release_s = float(e["FNIRSI_MCP_LEGACY_IDLE_RELEASE_S"])
        if e.get("FNIRSI_MCP_IDLE_CLOSE_S"):
            cfg.idle_close_s = float(e["FNIRSI_MCP_IDLE_CLOSE_S"])
        if "FNIRSI_MCP_AUDIT_LOG" in e:
            cfg.audit_log = Path(e["FNIRSI_MCP_AUDIT_LOG"]) if e["FNIRSI_MCP_AUDIT_LOG"] else None
        for k, v in override.items():
            if v is not None:
                setattr(cfg, k, v)
        return cfg


# ---------------------------------------------------------------------------------------------------------
# Static profile (docs/mcp-tools.md section 3)

STATIC_PROFILE: dict[str, str] = {
    "max_sample_rate": "200 MS/s per channel, both channels at once (2 x 100 MS/s interleaved cores)",
    "rate_formula": "fs = 200 MHz / (N + 1), N = 0 .. 2^32-1",
    "resolution": "8 bit, about 2 LSB noise; averaging n captures gives at most log2(sqrt(n)) extra bits and "
                  "only while noise dithers (>= ~0.5 LSB rms)",
    "analog_bandwidth": "about 20-40 MHz (-3 dB) depending on unit; treat 30 MHz as the limit, not 100 MHz",
    "interleave_artefacts": "A/B core offset (1.4-5.5 codes before compensation) gives a spur at fs/2; gain/"
                            "timing mismatch gives an image at fs/2 - f",
    "memory_per_channel": "stock FPGA 0x1432: 3000 samples (fixed 50 % trigger); Atlan4 AL3 0x1532: up to "
                          "16382; EF2 0x1632 / EP4 0x1732: up to 12286",
    "trigger": "edge on core A of CH1 or CH2, 8-bit level, quantised to 2 / fs; no hold-off. EXT (3.3 V logic on "
               "the calibration pin, shared with the generator) only on bitstream 0x1533+ with caps EXT_TRIGGER; "
               "EXT latency 20-25 ns from the pin, pulses >= 10 ns",
    "ranges_1x": "5 V, 2.5 V, 1 V, 500 mV, 200 mV, 100 mV per div (relays), 50 mV per div (software zoom of "
                 "100 mV, same ~3.4 mV per code)",
    "full_scale": "about 256 codes = ~8.8 divisions; e.g. ~0.88 V at 100 mV/div [U]",
    "coupling": "DC or AC (relay)",
    "input": "1 MOhm; assume 40 V peak maximum at 1x and use a 10x probe for anything higher [U]",
    "generator": "square wave only on the calibration pin, 11.6 mHz steps, 20 ns edge jitter, <= 25 MHz; only on "
                 "Atlan4 bitstreams. No DAC, no arbitrary waveform. At the probe it is a ~3.1 Vpp square wave whose "
                 "DC level is the USB ground, 1.24 V below the probe ground (reads -1.24 / +1.9 V): a timing reference, not "
                 "a voltage reference",
    "throughput": "~50-65 full dual-channel AL3 captures per second via raw reads; hundreds per second for "
                  "short windows [U USB rate]",
}

STANDING_CAVEATS = [
    "8-bit ADC, about 2 LSB noise per sample.",
    "Analog bandwidth about 30 MHz; content above that is attenuated.",
    "Trigger compares core A only: trigger jitter up to 2 / fs.",
    SCALING_CAVEAT,
]

#: T8 square-wave check (backups/t8sq_CH1.json): bench DC reads right, the calibration output does not swing 0..3.3 V
#: fpga-gen2, fpga/al3/README.md "Changes" 2 (commit 28126aa): probe ground = VRF1 = +1.24 V over USB ground
CAL_OUTPUT_NOTE = ("The calibration output is a frequency/duty/timing reference, not a voltage reference: a ~3.1 Vpp "
                   "square wave referenced to the scope's USB/digital ground, while the probe ground (BNC shells) "
                   "sits on VRF1, about +1.24 V above it (fpga/al3/README.md), so against the probe ground it reads "
                   "about -1.24 / +1.9 V. That is by design, not an error.")

EXT_NOTE = ("EXT triggers on the calibration pin, a 3.3 V logic input (never more than 3.3 V) referenced to the "
            "scope's USB/digital ground, NOT to the probe ground: the BNC shells sit on VRF1, about +1.24 V above USB "
            "ground (fpga/al3/README.md). Reference the EXT source to the USB connector shell; a source that shares the "
            "probe ground idles at ~1.24 V at the pin and is not recognised: put a red LED (or 3 x 1N4148) in series, "
            "anode at the source, and trigger on the rising edge. Never connect the pin to a detector coil, drain or "
            "damping resistor (flyback is hundreds of volts). The level is ignored (logic threshold); the trigger lags "
            "the pin by 20-25 ns (4-5 samples at 200 MS/s, under one sample at <= 10 MS/s); pulses under ~10 ns are "
            "rejected. The pin is also the generator output: EXT and scope_generator are mutually exclusive.")

DESC_SHARED = ("The scope is shared with the person at it (collaborative firmware): either side may change "
               "settings, the last change wins and the screen shows the host's captures.")
LEGACY_NOTE = ("legacy firmware without collaborative mode: the scope's touch UI is frozen while the host holds "
               "REMOTE; the server releases it (keeping the host's settings) after {idle:g} s without tool calls and "
               "at exit")

# ---------------------------------------------------------------------------------------------------------
# Session


class Session:
    """One connection to the scope, shared by all tool calls (serialised by a lock)."""

    def __init__(self, cfg: ServerConfig):
        self.cfg = cfg
        self.scope: Scope | None = None
        self.lock = threading.Lock()  # plain lock: async tools acquire it in a worker and release on the loop
        self.expect_remote = False
        self.tokens: dict[str, tuple[str, float, str]] = {}
        self.relay_times: collections.deque[float] = collections.deque()
        self.last_capture: Capture | None = None
        self.last_capture_file: str | None = None
        #: the Bode tool shows its wiring text with the first sweep of a session
        self.bode_wiring_shown = False
        #: end of the last tool's device work (its start while one runs); the idle rules count from here
        self.last_tool = time.monotonic()
        #: times the idle rule closed the port (diagnostics, tests)
        self.idle_closes = 0
        self._stop = threading.Event()
        self._ka = threading.Thread(target=self._keepalive, name="fnirsi-keepalive", daemon=True)
        self._ka.start()

    # connection -----------------------------------------------------------------------------------------

    def get_scope(self) -> Scope:
        if self.scope is None:
            try:
                self.scope = Scope.connect(self.cfg.port, max_response=self.cfg.max_response)
            except TransportError as e:
                raise ToolError(f"cannot connect to the scope at {self.cfg.port!r}: {e} (if another program holds "
                                "the port, close it and retry)") from e
            p = self.cal_path()
            if p.is_file():
                try:
                    self.scope.unit_cal = UnitCalibration.load(p)
                except (OSError, ValueError, KeyError) as e:
                    log.warning("calibration file %s not usable: %s", p, e)
        return self.scope

    def cal_path(self) -> Path:
        if self.cfg.cal_file is not None:
            return Path(self.cfg.cal_file)
        serial = self.scope.info.serial_str if self.scope is not None else "unit"
        safe = "".join(ch if ch.isalnum() or ch in "-_" else "_" for ch in serial) or "unit"
        return Path(self.cfg.data_dir) / "cal" / f"{safe}.json"

    def touch(self) -> None:
        """A tool's device work ended (called while it still holds the lock): restart the idle clocks."""
        self.last_tool = time.monotonic()

    def drop_connection(self) -> None:
        if self.scope is not None:
            try:
                self.scope.close()
            except Exception:  # noqa: BLE001
                pass
        self.scope = None

    def close(self) -> None:
        self._stop.set()
        with self.lock:
            if self.scope is not None and self.scope.remote:
                try:
                    self.scope.end_control(keep_settings=True)
                except (TransportError, DeviceError):
                    pass
            self.drop_connection()

    def _keepalive(self) -> None:
        """Legacy firmware: send GET_STATUS while REMOTE and idle so the firmware watchdog does not expire, and
        release REMOTE (keeping the host settings) once no tool has run for ``legacy_idle_release_s``. Any
        firmware: close the port once no tool has run for ``idle_close_s`` (``idle_close_check``)."""
        while not self._stop.wait(min(self.cfg.keepalive_s / 2, 1.0)):
            self.legacy_idle_check()
            self.idle_close_check()

    def idle_close_check(self, now: float | None = None) -> bool:
        """Close the connection when no tool has run for ``idle_close_s`` (0 = never) and the scope is not held in
        REMOTE (legacy firmware releases REMOTE first by its own rule). A running tool holds the session lock for
        its whole duration (averaged runs and sweeps included) and touches ``last_tool`` at its end, so it is never
        cut off. The next tool call reconnects through ``get_scope`` (calibration file reloaded). Returns True
        when it closed."""
        limit = self.cfg.idle_close_s
        s = self.scope
        if not limit or s is None or s.remote:
            return False
        now = time.monotonic() if now is None else now
        if now - self.last_tool <= limit or not self.lock.acquire(blocking=False):
            return False
        try:
            if self.scope is not s or s.remote or now - self.last_tool <= limit:
                return False  # a tool ran between the test and the lock
            self.drop_connection()
            self.idle_closes += 1
            log.info("closed the scope connection after %.0f s without tool calls (reopens on the next call)", limit)
            return True
        finally:
            self.lock.release()

    def legacy_idle_check(self, now: float | None = None) -> None:
        s = self.scope
        if s is None or not s.remote:
            return
        now = time.monotonic() if now is None else now
        idle = self.cfg.legacy_idle_release_s
        release = bool(idle) and now - self.last_tool > idle
        if not release and now - s.t.last_activity < self.cfg.keepalive_s:
            return
        if self.lock.acquire(blocking=False):
            try:
                if release:
                    s.end_control(keep_settings=True)
                    self.expect_remote = False
                    log.info("legacy firmware: released REMOTE after %.0f s idle (host settings kept)", idle)
                else:
                    s.status()
            except Exception:  # noqa: BLE001
                pass
            finally:
                self.lock.release()

    # control --------------------------------------------------------------------------------------------

    def ensure_control(self) -> list[str]:
        """Make the scope accept SET_*/ARM/READ_*. Collaborative firmware (SHARED_MODE): nothing to take; a modal menu
        blocks only before protocol 1.23 (fw 0.2.78 closes it for the request). Legacy firmware: acquire REMOTE if
        needed. Returns caveats describing what happened."""
        s = self.get_scope()
        self.last_tool = time.monotonic()
        st = s.status()
        deadline = time.monotonic() + self.cfg.starting_wait_s
        while st.ui_busy == P.UiBusy.STARTING and time.monotonic() < deadline:
            time.sleep(0.25)  # USB is up before the main loop's first pass (about 5 s after a FEL load)
            st = s.status()
        if st.ui_busy == P.UiBusy.STARTING:
            raise ToolError(f"the scope is still starting after {self.cfg.starting_wait_s:g} s (first UI pass not "
                            "finished); check the screen and retry")
        menu_note = []
        if st.ui_busy:
            if not s.shared or s.info.proto_minor < P.YIELD_MINOR:
                raise ToolError("scope UI is in a modal screen (menu, calibration or USB storage); close it on the "
                                "scope and retry (firmware before 0.2.78; newer firmware closes it by itself)")
            menu_note.append(MENU_CLOSED_NOTE)
        if s.shared:
            notes = menu_note
            if st.display_mode in (1, 2, 3):
                notes.append(SPECTRUM_VIEW_NOTE)
            elif st.display_mode == P.DisplayMode.PHOSPHOR:
                notes.append(PHOSPHOR_VIEW_NOTE)
            elif st.display_mode == P.DisplayMode.BODE:
                notes.append(BODE_VIEW_NOTE)
            notes += self.enc_notes(s)
            return notes
        if st.control == P.Control.REMOTE:
            s.remote = True
            return []
        notes = []
        if self.expect_remote:
            self.expect_remote = False
            if st.exit_reason == P.ExitReason.USER_TAP:
                raise ToolError("scope released by user (tap on the REMOTE banner of this legacy firmware). Someone "
                                "at the scope took it back; ask them before calling an acquisition tool again.")
            if st.exit_reason:
                notes.append(f"scope had returned to LOCAL ({P.ExitReason(st.exit_reason).name}); re-acquired, "
                             "host settings were lost and the user's settings are active again")
        s.legacy_take_remote(watchdog_s=self.cfg.watchdog_s)
        self.expect_remote = True
        notes.append("remote_acquired: " + LEGACY_NOTE.format(idle=self.cfg.legacy_idle_release_s))
        return notes

    @staticmethod
    def enc_notes(s: Scope) -> list[str]:
        """Protocol 1.19: a caveat while the scope's last full-rate capture was accepted garbled (GET_HEALTH 90 bit
        2, cleared by the next good one) or its encode convention check failed at boot."""
        if s.info.proto_minor < 19:
            return []
        try:
            h = s.health()
        except (DeviceError, TransportError):
            return []
        if h is None or not h.has_enc_check:
            return []
        out = []
        if h.enc_flags & P.ENC_LAST_BAD:
            pins = ", ".join(P.enc33_bad_pins(h.enc_last33)) if h.enc_flags & P.ENC_READ else ""
            out.append("the scope's last 200 MSa/s capture was accepted garbled after 16 re-arms (encode check"
                       f"{': ' + pins if pins else ''}); a full-rate (200 MSa/s, filter off) result may be garbled "
                       "on one ADC core: repeat it and check scope_health")
        if h.enc_flags & P.ENC_CONVENTION_BAD:
            out.append(f"the FPGA's encode convention check failed at boot (0x33 = 0x{h.enc_boot33:02X}): full-rate "
                       "core order is unknown; see scope_health")
        if h.has_capture and h.cap_flags & P.CAPF_FLAGGED:  # 1.24: the 0x153A self-check
            cores = ", ".join(f"{n} {h.cap_counts[P.CAP_CORES.index(n)]} outliers"
                              for n in P.cap_core_names(h.cap_flagged))
            out.append(f"the scope's last 200 MSa/s capture was flagged by the ADC capture self-check ({cores}): a "
                       "full-rate result may hold wrong samples on that core; run the calibration wizard (it trains "
                       "the capture stages) and check scope_health")
        return out

    # tokens ---------------------------------------------------------------------------------------------

    @staticmethod
    def _args_hash(tool: str, args: dict[str, Any]) -> str:
        blob = json.dumps({"tool": tool, "args": args}, sort_keys=True, default=str)
        return hashlib.sha256(blob.encode()).hexdigest()

    def confirm(self, tool: str, args: dict[str, Any], token: str | None, summary: str) -> dict[str, Any] | None:
        """Return None if ``token`` confirms exactly these args; otherwise a confirmation request."""
        now = time.monotonic()
        for k in [k for k, v in self.tokens.items() if v[1] < now]:
            del self.tokens[k]
        h = self._args_hash(tool, args)
        if token:
            entry = self.tokens.pop(token, None)
            if entry and entry[0] == h:
                return None
            reason = "expired or unknown token" if entry is None else "token was issued for different arguments"
        else:
            reason = "no confirm_token given"
        tok = secrets.token_hex(4)
        self.tokens[tok] = (h, now + self.cfg.token_ttl_s, summary)
        return {"ok": False, "confirmation_required": True, "token": tok, "summary": summary,
                "expires_in_s": self.cfg.token_ttl_s,
                "how_to_confirm": f"Repeat the same {tool} call with confirm_token='{tok}' and identical arguments.",
                "reason": reason}

    # relay rate limit -----------------------------------------------------------------------------------

    def relay_slot(self) -> float:
        """Block until a relay change is allowed (<= relay_changes_per_s per second). Returns the wait."""
        waited = 0.0
        while True:
            now = time.monotonic()
            while self.relay_times and now - self.relay_times[0] >= 1.0:
                self.relay_times.popleft()
            if len(self.relay_times) < self.cfg.relay_changes_per_s:
                self.relay_times.append(now)
                return waited
            d = 1.0 - (now - self.relay_times[0]) + 0.001
            time.sleep(d)
            waited += d

    # audit ----------------------------------------------------------------------------------------------

    def audit(self, tool: str, tier: int, args: dict[str, Any], status: str, applied: Any = None) -> None:
        if self.cfg.audit_log is None or tier < 1:
            return
        try:
            self.cfg.audit_log.parent.mkdir(parents=True, exist_ok=True)
            rec = {"time": _dt.datetime.now().isoformat(timespec="milliseconds"), "tool": tool, "tier": tier,
                   "args": args, "status": status}
            if applied is not None:
                rec["applied"] = applied
            with open(self.cfg.audit_log, "a", encoding="utf-8") as f:
                f.write(json.dumps(rec, default=str) + "\n")
        except OSError as e:
            log.warning("audit log write failed: %s", e)

    # data files -----------------------------------------------------------------------------------------

    def save_npz(self, kind: str, arrays: dict[str, np.ndarray], meta: dict[str, Any]) -> str:
        now = _dt.datetime.now()
        d = Path(self.cfg.data_dir) / now.strftime("%Y-%m-%d")
        d.mkdir(parents=True, exist_ok=True)
        cid = meta.get("capture_id", "")
        path = d / f"{now.strftime('%H%M%S_%f')[:-3]}_{kind}{f'_c{cid}' if cid != '' else ''}.npz"
        np.savez_compressed(path, meta_json=np.array(json.dumps(meta, default=str)), **arrays)
        return str(path)


class _SettingsChanged(Exception):
    """A capture of a long run was taken under another settings_seq (collaborative firmware)."""

    def __init__(self, done: int, seq: int | None):
        super().__init__(f"settings changed after {done} captures")
        self.done = done
        self.seq = seq


class _Held:
    """The session lock held across a long run's awaits; releasing it counts as tool activity (idle clocks)."""

    def __init__(self, sess: Session):
        self.sess = sess
        self.got = False

    def acquire(self) -> None:
        self.sess.lock.acquire()
        self.got = True

    def release(self) -> None:
        if self.got:
            self.got = False
            self.sess.touch()
            self.sess.lock.release()


# ---------------------------------------------------------------------------------------------------------
# Helpers


#: caveat while the scope's screen shows its spectrum view (protocol 1.7, GET_STATUS display_mode)
#: caveat while the scope shows its phosphor view (protocol 1.13, display_mode 4)
PHOSPHOR_VIEW_NOTE = ("the scope's screen shows its phosphor view: the person sees a persistence picture of many "
                      "captures (a host single is added once); the data returned here are unaffected")
#: Protocol 1.23 (fw 0.2.78): a menu, fold-out or dialog was open; the request closed it by its cancel path
MENU_CLOSED_NOTE = ("a menu or dialog was open on the scope: the firmware closed it for this request without applying "
                    "anything in it; tell the person if they were using it")
SPECTRUM_VIEW_NOTE = ("the scope's screen shows its spectrum view: the person at the scope sees the spectrum of each "
                      "capture, not the trace (the data returned here are unaffected; scope_spectrum reads what the "
                      "screen shows)")
BODE_VIEW_NOTE = ("the scope's screen shows its Bode plot (protocol 1.20): the scope's own acquisition is off; a host "
                  "capture ends a device sweep in progress (READ_BODE reason 5). The person leaves it with Menu > "
                  "Scope; scope_bode_sweep(where='device', close_view=true) closes it and gives the settings back")


def _spectrum_cross_check(s: Scope, ch: str, sp: Spectrum) -> dict[str, Any]:
    """Recompute the scope's spectrum on the host from the same capture's trace (READ_TRACE): the on-scope FFT
    model (codes minus the zero code, same window and N) against the device's levels in dB re 1 code RMS, and
    scope_capture_and_fft's computation (calibrated volts, analysis.amplitude_spectrum) against its dBV levels."""
    for _ in range(3):
        cap = s.read_trace_capture([ch])
        again = s.read_spectrum(ch, 0, 1)
        if again.header.frame == sp.header.frame:
            break
        sp = s.read_spectrum(ch)  # a new capture or setting replaced the spectrum meanwhile
    else:
        raise ToolError("scope_spectrum(cross_check): the spectrum kept changing (the scope is in RUN); stop it first")
    n = sp.n_fft
    c = cap.channels[ch]
    if c.codes.size < n:
        raise ToolError(f"scope_spectrum(cross_check): the trace has {c.codes.size} samples, the spectrum used {n}")
    if sp.header.first != 0 or sp.cdb.size != n // 2 + 1:
        raise ToolError("scope_spectrum(cross_check): needs the whole spectrum (first_bin 0, all bins)")
    win = P.SPECTRUM_WINDOWS.get(sp.header.window)
    if win is None:
        raise ToolError(f"scope_spectrum(cross_check): unknown window code {sp.header.window}")
    fft = A.compare_spectra(sp.db_codes, A.scope_fft_db(c.codes, round(c.config.zero_code), win, n))
    f, amp, _ = A.amplitude_spectrum(c.volts[:n], cap.dt, win, detrend=False)
    host = 20 * np.log10(np.maximum(amp, 1e-15))
    host[n // 2] -= 20 * np.log10(2)  # amplitude_spectrum doubles the Nyquist bin; the scope does not
    volts = A.compare_spectra(sp.db, host)
    vpl_db = 20 * math.log10(c.volts_per_lsb) if c.volts_per_lsb > 0 else None
    out = {"capture_id": cap.capture_id, "same_capture": True,
           "fft": {k: _r6(v) if isinstance(v, float) else v for k, v in fft.items()},
           "volts": {k: _r6(v) if isinstance(v, float) else v for k, v in volts.items()},
           "host_volts_per_lsb_db": _r6(vpl_db), "device_gain_db": sp.header.gain_cdb / 100,
           "scaling": c.scale.source if c.scale is not None else "firmware"}
    notes = []
    if fft["max_abs_diff_db"] is not None and fft["max_abs_diff_db"] > 0.2:
        notes.append(f"the on-scope FFT differs from the host model by up to {fft['max_abs_diff_db']:.2f} dB within "
                     "60 dB of the peak (expected < 0.1 dB)")
    if vpl_db is not None and abs(vpl_db - sp.header.gain_cdb / 100) > 0.5:
        notes.append(f"the scope labels levels with {sp.header.gain_cdb / 100:.2f} dB(V per code), the host's "
                     f"{out['scaling']} scaling gives {vpl_db:.2f} dB: absolute levels differ by "
                     f"{vpl_db - sp.header.gain_cdb / 100:+.2f} dB")
    out["notes"] = notes
    return out


def _err_text(tool: str, e: Exception) -> str:
    if isinstance(e, DeviceError):
        return f"{tool}: {e}"
    if isinstance(e, TransportError):
        return (f"{tool}: link error: {e}. The server will reconnect on the next call; if this repeats, check "
                "the USB cable and that the scope runs the USB-control firmware.")
    return f"{tool}: {e}"


def _channels_arg(channels: list[str] | str | None, default: list[str]) -> list[str]:
    if channels is None:
        return default
    if isinstance(channels, str):
        channels = [c for c in channels.replace(";", ",").split(",") if c.strip()]
    out = []
    for c in channels:
        out.append(CHANNEL_NAMES[channel_index(c)])
    if not out:
        raise ValueError("give at least one channel, e.g. ['CH1']")
    return list(dict.fromkeys(out))


def _q(x: float | str | None, unit: str | None = None) -> float | None:
    if x is None:
        return None
    return parse_quantity(x, unit)


def _r6(x: float | None) -> float | None:
    if x is None or not math.isfinite(x):
        return None
    return float(f"{x:.6g}")


def _window_arg(window: list[float | str] | None) -> tuple[float, float] | None:
    if window is None:
        return None
    if len(window) != 2:
        raise ValueError("window must be [t0, t1] in seconds relative to the trigger, e.g. ['-10us', '200us']")
    t0, t1 = (parse_quantity(w, "s") for w in window)
    if t1 <= t0:
        raise ValueError(f"window end {t1} must be after start {t0}")
    return t0, t1


def _sample_window(s: Scope, window: tuple[float, float] | None, margin: int = 0) -> tuple[int, int]:
    """(start_sample, count) relative to the capture start for a time window relative to the trigger."""
    trig = s.get_trigger()
    tb = s.get_timebase()
    total = trig.total_samples
    if window is None:
        return 0, total
    a = int(math.floor(window[0] / tb.dt)) + trig.pretrigger_samples - margin
    b = int(math.ceil(window[1] / tb.dt)) + trig.pretrigger_samples + 1 + margin
    a2, b2 = max(0, a), min(total, b)
    if b2 - a2 < 2:
        pre = trig.pretrigger_samples * tb.dt
        post = (total - trig.pretrigger_samples) * tb.dt
        raise ValueError(f"window {window[0]:.4g}..{window[1]:.4g} s is outside the capture, which spans "
                         f"{-pre:.4g}..{post:.4g} s around the trigger; change the window or set_trigger/"
                         "set_timebase")
    return a2, b2 - a2


def _volts_per_div_at_probe(rng: int, probe: float) -> float:
    return P.RANGE_VOLTS_PER_DIV[rng] * probe


def _channel_summary(s: Scope, ch: int) -> dict[str, Any]:
    cfg = s.get_channel(ch)
    cal = s.get_cal(ch)
    sc = s.scale(ch, cfg)
    probe = cfg.probe_factor
    vpl = sc.volts_per_lsb
    codes_per_div = P.RANGE_VOLTS_PER_DIV[cfg.range] * probe / vpl if vpl else 0.0
    zero = sc.zero_compensated
    offset_codes = zero - 128.0
    return {
        "channel": CHANNEL_NAMES[ch], "enabled": bool(cfg.enable), "range_index": cfg.range,
        "volts_per_div": _volts_per_div_at_probe(cfg.range, probe), "hw_range": cfg.hw_range,
        "software_zoom": cfg.range == 6, "coupling": "AC" if cfg.coupling else "DC", "probe": probe,
        "invert": bool(cfg.invert), "volts_per_lsb": _r6(vpl), "zero_code": _r6(zero), "scaling": sc.source,
        "full_scale_volts": [_r6((0 - zero) * vpl), _r6((255 - zero) * vpl)],
        "offset_div": _r6(offset_codes / codes_per_div) if codes_per_div else 0.0,
        "offset_volts": _r6(-offset_codes * vpl), "dcoffset": cfg.dcoffset, "offset_pwm": cfg.offset_pwm,
        "adc_a_comp": cal.adc_a_comp, "adc_b_comp": cal.adc_b_comp,
    }


def _timebase_summary(s: Scope) -> dict[str, Any]:
    tb = s.get_timebase()
    trig = s.get_trigger()
    win = trig.total_samples * tb.dt
    return {"sample_rate": _r6(1 / tb.dt), "dt": _r6(tb.dt), "divider": tb.divider,
            "ui_timeperdiv_index": None if tb.timeperdiv_index == 0xFF else tb.timeperdiv_index,
            "window": _r6(win), "time_per_div_equivalent": _r6(win / 10)}


def _scope_cal_summary(c: P.CalResp) -> dict[str, Any]:
    """GET_CAL's calibration state and stored v3 block (protocol 1.12, row 51) in codes and gain factors."""
    if not c.cal_version:
        return {"version": None, "note": "firmware before protocol 1.10 (no scope calibration block)"}
    if c.cal_version == 2:
        return {"version": 2, "in_use": "v2 (not decoded)",
                "note": "firmware 0.2.43-0.2.44 calibration v2: update to >= 0.2.45 and re-run the base calibration"}
    flags = [n for b, n in ((P.CAL_LOADED, "block_loaded"), (P.CAL_BOUND, "bound_to_pwm_centres"),
                            (P.CAL_GATED, "current_divider_gated"), (P.CAL_GAINS_MEASURED, "gains_measured"),
                            (P.CAL_GAINS_KEPT, "gains_kept_from_earlier_run")) if c.cal_flags & b]
    out: dict[str, Any] = {
        "version": c.cal_version, "in_use": "v3" if c.v3 else "1.00o5 compensations", "flags": flags,
        "current": {"comp": {"A": c.comp_a + c.frac_a / 16, "B": c.comp_b + c.frac_b / 16},
                    "gain": {"A": _r6(c.gain_a / 65536), "B": _r6(c.gain_b / 65536)},
                    "offset_codes": {"A": _r6(c.off_a / 65536), "B": _r6(c.off_b / 65536)},
                    "gain16": _r6(c.gain16 / 65536), "zero_code": _r6(c.cal_zero_q8 / 256),
                    "corr16_codes": _r6(c.corr16 / 16), "shift": c.cal_shift}}
    if c.cal_flags & P.CAL_LOADED:
        names = P.CAL_CLASS_NAMES
        measured = c.gain_range != P.CAL_GAIN_NONE
        out["stored"] = {
            "pwm_centres": list(c.pwm),
            "zero_codes": {names[k]: {core: [_r6(c.zero_raw(k, i, r)) for r in range(6)]
                                      for i, core in enumerate("AB")} for k in (0, 1)},
            "slope_codes_per_count": {names[k]: {core: [_r6(c.slope_codes(k, i, r)) for r in range(6)]
                                                 for i, core in enumerate("AB")} for k in (0, 1)},
            "noise_rms_codes": {names[k]: {core: [_r6(c.noise_rms(k, i, r)) for r in range(6)]
                                           for i, core in enumerate("AB")} for k in (0, 1)},
            "gain_correction": {names[k]: {core: _r6(c.gain_corr(k, i)) for i, core in enumerate("AB")}
                                for k in (0, 1)},
            "gain16_full": _r6(c.gain16_full() / 65536),
            "gains_measured_on_range": c.gain_range if measured else None,
            "gain_lever_codes": _r6(c.gain_lever / 16) if measured else None,
            "gains_carried_over": bool(c.gain_flags & 1)}
    if c.has_tracker:
        out["tracking"] = _tracker_summary(c)
    return out


def _provenance_summary(c: P.CalResp, info: P.InfoResp) -> dict[str, Any]:
    """GET_CAL 288..355 (protocol 1.16): where the scope's calibration came from, in words (one block for both
    channels)."""
    if c.prov_state == 0:
        return {"state": "none", "text": "no provenance on the card: calibrated before fw 0.2.52, or an older "
                                         "firmware saved sector 708 since (which clears the trailer)"}
    if c.prov_state != 1:
        return {"state": "unreadable", "text": "a provenance trailer is on the card but its CRC or version is wrong"}
    try:
        p = c.provenance()
    except ValueError as e:
        return {"state": "unreadable", "text": f"provenance: {e}"}
    assert p is not None
    when = p.time_str or "no clock module (no date)"
    since = []
    if c.prov_power_ons_since != P.CAL_AGE_NONE:
        since.append(f"{c.prov_power_ons_since} power-ons")
    if c.prov_minutes_since != P.CAL_AGE_NONE:
        since.append(f"{c.prov_minutes_since / 60:.1f} h of operation")
    same = p.build == info.fw_build and p.version == info.fw_version_str
    text = (f"calibrated by fw {p.version} (build {p.build:08x}, FPGA 0x{p.fpga:04X}){' (the running build)' if same else ''}"
            f", parts written: {', '.join(p.parts) or 'none'}; {when}; "
            f"{' and '.join(since) + ' ago' if since else 'age unknown'}")
    return {"state": "valid", "fw_version": p.version, "build": f"{p.build:08x}", "fpga": f"0x{p.fpga:04X}",
            "parts": p.parts, "time": p.time_str, "power_on": p.power_on,
            "power_ons_since": None if c.prov_power_ons_since == P.CAL_AGE_NONE else c.prov_power_ons_since,
            "operating_hours_since": None if c.prov_minutes_since == P.CAL_AGE_NONE else
            round(c.prov_minutes_since / 60, 2), "by_running_build": same, "text": text}


def _health_words(d: dict[str, Any], h: P.HealthResp) -> list[str]:
    u, cpu, life, st = d["usb"], d["cpu"], d["life"], d["settings"]
    w = [f"USB {u['text']}" + (", high speed" if u["high_speed"] else "") +
         (", port open" if u["port_open"] else ", port closed (DTR low)")]
    w.append(f"CPU: MMU {'on' if cpu['mmu'] else 'off'}, D-cache {'on' if cpu['dcache'] else 'off'}, I-cache "
             f"{'on' if cpu['icache'] else 'off'} (CP15 {cpu['cp15_control']})")
    if life["power_on"]:
        w.append(f"power-on #{life['power_on']}, {life['operating_hours']} h of operation in all, up "
                 f"{life['uptime_s']:.0f} s; the previous power-on {life['previous_power_on_ended']}")
    else:
        w.append("power-on count unknown: no valid settings sector at boot (nothing is counted until the first save)")
    if not life["settings_sector_valid_at_boot"]:
        w.append("the settings sector was not valid at boot: the 1.00o5 defaults were loaded")
    if st["last_save_failed"]:
        w.append(f"the last settings save failed (result {st['last_save_result']})")
    if st["changed_since_saved"]:
        w.append(f"settings changed since they were saved (checked {st['evaluated_s_ago']} s ago): they are saved "
                 "at power-off")
    r = d["restart_record"]
    if r is None:
        w.append("no stored restart record")
    else:
        when = "this power-on" if r["from_this_power_on"] else f"power-on #{r['power_on']}"
        w.append(f"stored restart record: {r['exception']} in {when} ({r['restarts']} restarts, at "
                 f"{r['operating_hours']} h; exception LR {r['registers']['exc_lr']}, fault address "
                 f"{r['registers']['far']}); it stays until cleared from the scope's Diagnostics")
    e = d.get("encode_check")
    if e is not None:  # protocol 1.19
        if e["mode"] == "none":
            w.append("full-rate capture check: none on this bitstream")
        else:
            w.append(f"full-rate capture check ({e['mode']}): {e['text']}; {e['captures_checked']} captures checked "
                     f"since boot, {e['captures_bad']} found garbled, {e['captures_recovered']} of those re-taken "
                     "clean" + (f", last 0x33 = {e['last_33']}" if e["last_33"] else ""))
    c = d.get("capture_stages")
    if c is not None and c["has_stages"]:  # protocol 1.24
        w.append(f"ADC {c['text']}")
    return w


def _age_text(ms: int) -> str:
    if ms == P.CAL_AGE_NONE:
        return "never since the reset"
    s = ms / 1000
    return f"{s:.1f} s ago" if s < 120 else f"{s / 60:.1f} min ago" if s < 7200 else f"{s / 3600:.1f} h ago"


#: a tracker that has not accepted a record for this long is reported as frozen
TRACK_FROZEN_S = 60.0


def _tracker_summary(c: P.CalResp) -> dict[str, Any]:
    """GET_CAL bytes 256..287 (protocol 1.15, row 54) in words: the full-rate A - B tracker and the class probe."""
    f = c.track_flags
    words = []
    if not f & P.TRACK_RECORD:
        state = "idle"
        words.append("no record accepted since the reset (boot or the last calibration): the stored v3 values alone "
                     "apply")
    elif c.track_age_ms != P.CAL_AGE_NONE and c.track_age_ms / 1000 > TRACK_FROZEN_S:
        state = "frozen"
        words.append(f"frozen: the last accepted record was {_age_text(c.track_age_ms)}; the values are held")
    else:
        state = "tracking"
        words.append(f"tracking: {c.track_records} records, the last {_age_text(c.track_age_ms)}")
    if f & P.TRACK_RECORD and not f & P.TRACK_LAST_USABLE:
        words.append(f"the last offered record was not usable ({c.track_last_pairs} pairs: needs >= 256 samples "
                     "within 4..251 on gentle slopes; fast edges or energy near fs/2 reject it, as does the scope "
                     "not running at 200 MSa/s with the filter off)")
    if f & P.TRACK_RECORD and not f & P.TRACK_E:
        words.append(f"slope e held: the input's spread of codes is {c.track_spread16 / 16:.1f} RMS (< 8 needed)")
    if not f & P.TRACK_APPLIES:
        words.append("not applied to this channel's next 8-bit readout (needs a bound v3 calibration, 200 MSa/s and "
                     "the filter off at the last arm); cm still applies to full-rate and filtered readouts")
    if f & P.TRACK_PROBE_DATA:
        cm = f"common mode {c.track_cm:+.2f} code from {c.cm_probes} probes, the last {_age_text(c.cm_age_ms)}"
        if not f & P.TRACK_PROBE_VALID:
            cm += " (the last probe was not valid: the value is held)"
        words.append(cm)
    else:
        words.append("no valid class probe since the reset: cm = 0 (a probe needs a quiet input on this channel, the "
                     "scope running with no menu open and no host capture in flight)")
    return {"state": state, "applies_to_next_8bit_readout": bool(f & P.TRACK_APPLIES),
            "d_codes": _r6(c.track_d), "e": _r6(c.track_e),
            "q_per_code2": _r6(c.track_q), "q_identified": bool(f & P.TRACK_Q),
            "e_identified": bool(f & P.TRACK_E), "records": c.track_records,
            "age_s": None if c.track_age_ms == P.CAL_AGE_NONE else round(c.track_age_ms / 1000, 1),
            "last_pairs": c.track_last_pairs, "spread_codes": round(c.track_spread16 / 16, 2),
            "cm_codes": _r6(c.track_cm), "cm_probes": c.cm_probes,
            "cm_age_s": None if c.cm_age_ms == P.CAL_AGE_NONE else round(c.cm_age_ms / 1000, 1),
            "in_words": words}


def _decim_summary(st: P.DecimState) -> dict[str, Any]:
    """GET_DECIM / SET_DECIM in physical terms (protocol 1.9)."""
    d = st.next
    bits = A.decim_bits(d.k, d.n, d.gain, d.shift, fir=d.fir)
    out: dict[str, Any] = {
        "setting": st.filter_setting, "effective": d.filter_name, "k": d.k,
        "reason": P.DECIM_WHY[st.why] if st.why < len(P.DECIM_WHY) else st.why,
        "output_rate": st.rate_hz, "dt": _r6(1 / st.rate_hz) if st.rate_hz else None}
    if d.k:
        out.update({"n": d.n, "shift": d.shift, "gain": d.gain, "codes_per_count": _r6(d.code_per_count),
                    "noise_equivalent_n": _r6(A.decim_noise_n(d.k, d.n, d.fir)), "storage_bits": _r6(bits["storage"]),
                    "effective_bits_estimate": _r6(bits["effective"]), "settle_us": d.settle_us,
                    "max_total_samples": st.max_total})
        if st.ext_shift:
            out["ext_latency_shift_samples"] = st.ext_shift
        if d.fir_en and not d.fir:
            out["fir_note"] = "FIR requested but not active: it needs N >= 16 with N % 4 == 0 (10 MSa/s and slower)"
    if st.flags & 0x07:
        out["flags"] = [n for b, n in ((1, "core_order_swap"), (2, "falling_edge_A"), (4, "falling_edge_B"))
                        if st.flags & b]
    return out


DECIM_CAVEATS = [
    "the filter is the scope's acquisition Filter setting: shared with the person at the scope and saved with its "
    "settings; turn it off when done unless they want it",
    "200 MSa/s (divider 0) and captures over 8190 samples always use the legacy 8-bit path",
    "CIC3 is exact up to N = 10,334 (rates of about 19.4 kSa/s and above), CIC2 up to N = 1,050,630; slower rates "
    "fall back one order (reason says so)",
    "a clipped raw sample inside a filter window is not visible in the output: check the range with the filter off "
    "first"]


def _level_volts(s: Scope, src: int, level_code: int) -> float | None:
    if src > 1:
        return None
    return s.scale(src).volts_for_raw_a(level_code)


def _trigger_summary(s: Scope) -> dict[str, Any]:
    tr = s.get_trigger()
    tb = s.get_timebase()
    src = ("CH1", "CH2", "EXT")[tr.source] if tr.source < 3 else str(tr.source)
    out = {"source": src, "edge": P.EDGES[tr.edge] if tr.edge < 2 else tr.edge,
           "level_code": tr.level, "level_volts": _r6(_level_volts(s, tr.source, tr.level)),
           "mode": P.TRIG_MODES[tr.mode] if tr.mode < 3 else tr.mode,
           "pretrigger_samples": tr.pretrigger_samples, "total_samples": tr.total_samples,
           "pretrigger_fraction": _r6(tr.pretrigger_samples / tr.total_samples) if tr.total_samples else None,
           "pre_trigger_time": _r6(tr.pretrigger_samples * tb.dt),
           "post_trigger_time": _r6((tr.total_samples - tr.pretrigger_samples) * tb.dt),
           "auto_timeout_s": _r6(tr.auto_timeout * 2 * tb.dt), "holdoff": tr.holdoff}
    if tr.source <= 1:
        cfg = s.get_channel(tr.source)
        out["level_step_volts"] = _r6(s.scale(tr.source, cfg).gain_a * cfg.probe_factor)
    return out


def _gen_summary(s: Scope) -> dict[str, Any]:
    if not s.profile().has_generator:
        return {"available": False, "note": "stock FPGA: the calibration pin outputs a fixed 1 kHz square wave"}
    g = s.get_gen()
    return {"available": True, "enabled": bool(g.enable), "frequency": _r6(g.frequency_hz),
            "duty_percent": _r6(g.duty * 100)}


def _capture_caveats(cap: Capture, freq: dict[str, float | None]) -> list[str]:
    cav = []
    if cap.auto_timeout:
        cav.append("auto trigger fired: data not synchronised to the signal")
    if cap.decim is not None:
        d = cap.decim
        cav.append(f"decimated capture ({d.filter_name}, N = {d.n}): each sample is the filter output over "
                   f"{d.n * d.k - d.k + 1} raw samples ({fmt_si(5e-9 * (d.k * (d.n - 1) + 1), 's')}), resolution "
                   f"{d.code_per_count:.3g} codes; a clipped raw sample inside a window is not visible")
    for name, ch in cap.channels.items():
        n0 = int(np.sum(ch.codes <= 0))
        n255 = int(np.sum(ch.codes >= 255))
        nclip = int(ch.clipped.sum())
        if nclip:
            cav.append(f"{name}: {nclip} samples clipped (codes 0: {n0}, 255: {n255}): increase volts_per_div "
                       "or change the offset")
        f = freq.get(name)
        if f:
            if f > cap.sample_rate / 2.5:
                cav.append(f"{name}: frequency estimate {fmt_si(f, 'Hz')} is above fs/2.5 "
                           f"({fmt_si(cap.sample_rate / 2.5, 'Hz')}): aliasing likely, raise the sample rate")
            if f > 30e6:
                cav.append(f"{name}: frequency > 30 MHz is above the analog bandwidth")
    return cav


def _channel_result(cap: Capture, name: str, inline_max: int) -> dict[str, Any]:
    ch = cap.channels[name]
    st = A.stats(ch.volts)
    fr = A.frequency(ch.volts, cap.dt)
    out: dict[str, Any] = {k: _r6(v) for k, v in st.items()}
    out.update({"frequency": _r6(fr["frequency"]) if fr["frequency"] else None,
                "duty": _r6(fr["duty"]) if fr["duty"] is not None else None,
                "clipped_samples": int(ch.clipped.sum()), "volts_per_lsb": _r6(ch.volts_per_lsb),
                "scaling": ch.scale.source if ch.scale is not None else "firmware"})
    return out


def _save_capture(sess: Session, cap: Capture, kind: str, extra_meta: dict[str, Any] | None = None) -> str:
    arrays: dict[str, np.ndarray] = {"t": cap.t}
    for name, ch in cap.channels.items():
        arrays[f"{name}_volts"] = ch.volts.astype(np.float32)
        arrays[f"{name}_codes"] = ch.codes.astype(np.float32)
        arrays[f"{name}_clipped"] = ch.clipped
        if ch.raw_a is not None:
            arrays[f"{name}_raw_a"] = ch.raw_a
            arrays[f"{name}_raw_b"] = ch.raw_b  # type: ignore[assignment]
    for name, ch in cap.channels.items():
        if ch.samples16 is not None:
            arrays[f"{name}_samples16"] = ch.samples16
    meta = {"capture_id": cap.capture_id, "dt": cap.dt, "divider": cap.divider, "start_sample": cap.start_sample,
            "pretrigger_samples": cap.pretrigger_samples, "total_samples": cap.total_samples,
            "triggered": cap.triggered, "auto_timeout": cap.auto_timeout, "origin": cap.origin,
            "channels": {n: c.config.to_dict() for n, c in cap.channels.items()},
            "trigger": cap.trigger.to_dict(), "scaling": SCALING_CAVEAT}
    if cap.decim is not None:
        d = cap.decim
        meta["decimation"] = {"filter": d.filter_name, "k": d.k, "n": d.n, "shift": d.shift, "gain": d.gain,
                              "codes_per_count": d.code_per_count}
    if extra_meta:
        meta.update(extra_meta)
    return sess.save_npz(kind, arrays, meta)


def _capture_result(sess: Session, cap: Capture, kind: str) -> dict[str, Any]:
    path = _save_capture(sess, cap, kind)
    sess.last_capture, sess.last_capture_file = cap, path
    freq = {}
    chans = {}
    for name in cap.channels:
        r = _channel_result(cap, name, sess.cfg.inline_max_points)
        freq[name] = r["frequency"]
        chans[name] = r
    out: dict[str, Any] = {
        "ok": True, "capture_id": cap.capture_id, "origin": cap.origin,
        "trigger": "fired" if cap.triggered else ("auto_timeout" if cap.auto_timeout else "none"),
        "dt": _r6(cap.dt), "sample_rate": _r6(cap.sample_rate), "n_samples": cap.n,
        "t_start": _r6(float(cap.t[0])) if cap.n else None, "t_end": _r6(float(cap.t[-1])) if cap.n else None,
        "channels": chans, "file": path,
    }
    if cap.n <= sess.cfg.inline_max_points:
        out["samples"] = {"t": A._r(cap.t), **{n: A._r(c.volts) for n, c in cap.channels.items()}}
    else:
        out["preview"] = {n: A.preview(cap.t, c.volts) for n, c in cap.channels.items()}
    if cap.settings_seq is not None:
        out["settings_seq"] = cap.settings_seq
    if cap.capture_info is not None:
        out["capture_settings"] = _capture_settings_summary(cap)
        h = cap.capture_info.header
        if h.trig_source == 2:
            out["ext_edge_seen"] = bool(h.flags & h.F_EXT_EDGE)
    if cap.decim is not None:
        d = cap.decim
        out["decimation"] = {"filter": d.filter_name, "n": d.n, "shift": d.shift,
                             "codes_per_count": _r6(d.code_per_count)}
    out["caveats"] = list(cap.notes) + _capture_caveats(cap, freq) + STANDING_CAVEATS
    return out


def _capture_settings_summary(cap: Capture) -> dict[str, Any]:
    """The settings a capture was taken with (collaborative firmware), which may differ from the live ones."""
    ci = cap.capture_info
    assert ci is not None
    h = ci.header
    chans = {}
    for i, c in enumerate(ci.channels):
        cfg = ci.channel_config(i)
        chans[CHANNEL_NAMES[i]] = {"enabled": bool(c.enable), "volts_per_div": _volts_per_div_at_probe(c.range,
                                   cfg.probe_factor), "range_index": c.range, "coupling": "AC" if c.coupling else "DC",
                                   "probe": cfg.probe_factor, "invert": bool(c.invert)}
    return {"sample_rate": _r6(1 / cap.dt), "divider": h.divider, "channels": chans,
            "trigger": {"source": ("CH1", "CH2", "EXT")[h.trig_source] if h.trig_source < 3 else h.trig_source,
                        "edge": P.EDGES[h.trig_edge] if h.trig_edge < 2 else h.trig_edge, "level_code": h.trig_level,
                        "mode": P.TRIG_MODES[h.trig_mode] if h.trig_mode < 3 else h.trig_mode,
                        "pretrigger_samples": h.pretrigger_samples, "total_samples": h.total_samples}}


def _control_name(st: P.StatusResp) -> str:
    try:
        return P.Control(st.control).name
    except ValueError:
        return str(st.control)


# ---------------------------------------------------------------------------------------------------------
# Server construction


def build_server(cfg: ServerConfig | None = None) -> tuple[FastMCP, Session]:
    cfg = cfg or ServerConfig.from_env()
    sess = Session(cfg)
    mcp = FastMCP(
        "fnirsi1013d_mcp",
        instructions=(
            "Controls a FNIRSI 1013D tablet oscilloscope (8-bit, 2 channels, up to 200 MS/s, ~30 MHz analog "
            "bandwidth) running the open firmware with the USB control protocol. Typical metal-detector workflow: "
            "scope_set_channel, scope_set_timebase, scope_set_trigger (trigger on the TX gate on CH2), then "
            "scope_averaged_capture or scope_decay_fit. The scope is shared with the person at it: either side "
            "can change settings (the screen shows host captures; long runs restart and say so when a setting "
            "is changed at the scope). "
            "Waveforms are saved as .npz files; results carry statistics, a preview and caveats. Read the "
            "caveats: they say when data is clipped, aliased or unsynchronised."),
    )

    def tier_enabled(tier: int) -> bool:
        if tier >= 4:
            return cfg.enable_raw and cfg.max_tier >= 4
        return tier <= cfg.max_tier

    def register(tier: int, *, title: str, read_only: bool = False, destructive: bool = False,
                 idempotent: bool = False, open_world: bool = False) -> Callable[[Callable[..., Any]], Any]:
        def deco(fn: Callable[..., Any]) -> Any:
            if not tier_enabled(tier):
                return fn
            name = fn.__name__
            fn.__doc__ = (fn.__doc__ or "").strip() + f"\n\nSafety tier {tier}."
            return mcp.tool(name=name, title=title, annotations=ToolAnnotations(
                title=title, readOnlyHint=read_only, destructiveHint=destructive, idempotentHint=idempotent,
                openWorldHint=open_world))(fn)
        return deco

    async def run(tool: str, tier: int, args: dict[str, Any], fn: Callable[[], T]) -> T:
        """Run blocking device work under the session lock in a worker thread, with error mapping and audit."""
        def locked() -> T:
            with sess.lock:
                try:
                    return fn()
                except TransportError:
                    sess.drop_connection()
                    raise
                finally:
                    sess.touch()
        try:
            result = await anyio.to_thread.run_sync(locked)
        except ToolError as e:
            sess.audit(tool, tier, args, f"error: {e}")
            raise
        except DeviceError as e:
            sess.audit(tool, tier, args, f"error: {e}")
            raise ToolError(_err_text(tool, e)) from e
        except (TransportError, ValueError, ScopeError) as e:
            sess.audit(tool, tier, args, f"error: {e}")
            raise ToolError(_err_text(tool, e)) from e
        applied = result.get("applied") if isinstance(result, dict) else None
        sess.audit(tool, tier, args, "ok" if not (isinstance(result, dict) and result.get("confirmation_required"))
                   else "confirmation_required", applied)
        return result

    def need_tier(tier: int, what: str) -> None:
        if not tier_enabled(tier):
            raise ToolError(f"{what} needs safety tier {tier}, but this server allows at most tier {cfg.max_tier} "
                            "(FNIRSI_MCP_MAX_TIER)")

    # ---- tier 0: session and status ----------------------------------------------------------------------

    @register(0, title="Scope identity", read_only=True, idempotent=True)
    async def scope_info() -> dict[str, Any]:
        """Identify the connected scope: firmware version and build, protocol version, FPGA version word and
        variant ('stock', 'al3', 'ef2', 'ep4'), serial, capability flags by name, ring size and memory limits.

        If fpga_variant is 'stock', pre-trigger and total samples are fixed (1500 / 3000) and there is no
        generator."""
        def go() -> dict[str, Any]:
            s = sess.get_scope()
            i = s.identify()
            p = s.profile()
            return {"ok": True, "link": s.t.link.description, "fw_version": i.fw_version_str,
                    "fw_build": f"{i.fw_build:08x}", "protocol": f"{i.proto_major}.{i.proto_minor}",
                    "fpga_version": f"0x{i.fpga_version:04X}", "fpga_variant": p.variant, "serial": i.serial_str,
                    "caps": p.caps, "ring_words": i.ring_words, "max_total_samples": i.max_total_samples,
                    "min_total_samples": i.min_total_samples, "read_lag": i.read_lag,
                    "interleave_order": "B before A" if i.interleave_order == 0 else "A before B",
                    "generator_clock_hz": i.gen_clock_hz, "max_response_payload": i.max_response_payload,
                    "caveats": (["stock FPGA: pre-trigger and total samples are fixed (1500 / 3000), no "
                                 "generator"] if p.variant == "stock" else [])}
        return await run("scope_info", 0, {}, go)

    @register(0, title="Scope capability profile", read_only=True, idempotent=True)
    async def scope_profile() -> dict[str, Any]:
        """Capability profile with standing caveats: sample-rate formula, resolution, bandwidth, interleave
        artefacts, memory per FPGA variant, trigger limits, ranges, generator, throughput, merged with live
        values (current memory limit, which bitstream, which facts are still unverified: read_lag, interleave
        order, FORCE). Read this once before planning measurements."""
        def go() -> dict[str, Any]:
            s = sess.get_scope()
            p = s.profile()
            i = p.info
            unverified = []
            if i.caps & P.Caps.READ_LAG_UNVERIFIED:
                unverified.append("read_lag is a default, not measured (bench test T3)")
            if i.caps & P.Caps.INTERLEAVE_UNVERIFIED:
                unverified.append("A/B interleave order is the 1.00o5 convention, not measured (T4)")
            if not i.caps & P.Caps.FORCE_TRIGGER:
                unverified.append("FORCE not verified on this variant (T7): use trigger mode 'auto' instead")
            unverified.append("trigger_addr alignment +-1 word (T5); voltage scaling (T8)")
            return {"ok": True, "static": STATIC_PROFILE,
                    "live": {"fpga_variant": p.variant, "fpga_version": f"0x{i.fpga_version:04X}",
                             "max_total_samples": i.max_total_samples, "min_total_samples": i.min_total_samples,
                             "programmable_trigger_position": p.programmable, "generator": p.has_generator,
                             "device_averaging": p.has(P.Caps.DEVICE_AVERAGE), "force": p.has(P.Caps.FORCE_TRIGGER),
                             "screenshot": p.has(P.Caps.SCREENSHOT), "ext_trigger": p.has(P.Caps.EXT_TRIGGER),
                             "segments_and_hardware_averaging": p.has(P.Caps.SEQ), "caps": p.caps},
                    "unverified": unverified, "caveats": STANDING_CAVEATS}
        return await run("scope_profile", 0, {}, go)

    @register(0, title="Scope status", read_only=True)
    async def scope_status() -> dict[str, Any]:
        """Mode ('collaborative': shared with the person at the scope; 'exclusive' on legacy firmware with
        LOCAL/REMOTE), display_mode (what the trace area shows: scope, spectrum, waterfall, density (protocol
        1.7, the spectrum view and its sub-views), phosphor (1.13) or bode (1.20, the scope's Bode plot); a number for
        a mode this host does not know), from protocol 1.17 the bitstream's features (ext_trigger, decim, fir,
        segments, hw_average; enc_check 1.19; bode 1.20) and the
        acquisition menu's Average (ui_averaging off / software / hardware, hardware_averaging, records per run),
        run_state (RUN / STOP / SINGLE), settings_seq (changes whenever anyone changes a setting),
        the latest capture's id and origin (host / ui_run / ui_single), the host capture state, trigger address,
        battery, and on legacy firmware the watchdog and exit_reason. Cheap; use it to poll."""
        def go() -> dict[str, Any]:
            s = sess.get_scope()
            st = s.status()
            b = s.battery()
            flags = [f.name for f in P.StatusFlags if st.flags & f]
            extra: dict[str, Any] = {}
            if s.profile().has(P.Caps.EXT_TRIGGER):
                extra_ext = {"ext": {"level": "high" if st.ext_flags & P.ExtFlags.LEVEL else "low",
                                     "generator_drives_pin": bool(st.ext_flags & P.ExtFlags.GENERATOR),
                                     "edge_seen_since_arm": bool(st.ext_flags & P.ExtFlags.EDGE_SEEN)}}
                if st.ext_flags & P.ExtFlags.GENERATOR and s.get_trigger(cached=False).source == 2:
                    # possible through the scope's own generator switch, which the firmware does not block
                    extra_ext["ext"]["warning"] = ("generator on while the trigger source is EXT: the scope triggers "
                                                   "on its own generator output, not on an external signal")
            else:
                extra_ext = {}
            if st.shared:
                try:
                    disp = P.DisplayMode(st.display_mode).name.lower()
                except ValueError:
                    disp = st.display_mode
                extra = {"mode": "collaborative", "display_mode": disp, "run_state": P.RunState(st.run_state).name,
                         "settings_seq": st.settings_seq, "capture_settings_seq": st.capture_settings_seq,
                         "capture_origin": P.CaptureOrigin(st.capture_origin).name.lower()}
                if s.info.proto_minor >= 17:  # 1.17: the bitstream's features and the acquisition menu's Average
                    extra["features"] = P.feature_names(st.features)
                    extra["ui_averaging"] = P.UI_AVERAGE_NAMES.get(st.ui_average, st.ui_average)
                    extra["hardware_averaging"] = st.ui_average == 2
                    if st.ui_average == 2:
                        extra["ui_averaging_records"] = 1 << st.ui_average_log2n
            else:
                extra = {"mode": "exclusive (legacy firmware)"}
            return {"ok": True, **extra, **extra_ext, "control": _control_name(st),
                    "acq_state": P.AcqState(st.acq_state).name,
                    "flags": flags, "ui_busy": bool(st.ui_busy), "capture_id": st.capture_id,
                    "trigger_addr": st.trigger_addr, "capture_start_word": st.capture_start_word,
                    "ms_since_arm": st.ms_since_arm, "uptime_s": st.uptime_ms / 1000,
                    "watchdog_remaining_s": None if st.watchdog_remaining_s == 0xFFFF else st.watchdog_remaining_s,
                    "main_loop_pass_ms": st.pass_ms or None, "main_loop_pass_max_ms": st.pass_max_ms or None,
                    "battery_level": b.level, "charging": bool(b.charging),
                    "exit_reason": P.ExitReason(st.exit_reason).name if st.exit_reason else None,
                    "link_stats": vars(s.t.stats), "device_link_stats": s.link_stats()}
        return await run("scope_status", 0, {}, go)

    @register(0, title="Read configuration", read_only=True, idempotent=True)
    async def scope_get_config() -> dict[str, Any]:
        """Both channels (enabled, V/div at the probe, coupling, offset in divisions and volts, probe factor,
        invert, volts per code, full-scale span), timebase (rate, dt, divider, window, time/div equivalent),
        trigger (source, edge, level in volts and code, mode, pre-trigger samples and fraction, total samples,
        window durations) and generator: the live settings, which the person at the scope may change at any time
        (collaborative firmware; settings_seq tells whether they did)."""
        def go() -> dict[str, Any]:
            s = sess.get_scope()
            s.invalidate()
            if s.shared:
                ctx = s.capture_context()
                head: dict[str, Any] = {"control": "SHARED", "settings_seq": ctx.settings_seq}
            else:
                head = {"control": "REMOTE" if s.remote else "LOCAL"}
            out = {"ok": True, **head, "channels": [_channel_summary(s, 0), _channel_summary(s, 1)],
                   "timebase": _timebase_summary(s), "trigger": _trigger_summary(s), "generator": _gen_summary(s)}
            if s.profile().has(P.Caps.DECIM):
                out["decimation"] = _decim_summary(s.get_decim())
            out["caveats"] = [SCALING_CAVEAT]
            return out
        return await run("scope_get_config", 0, {}, go)

    @register(0, title="Read calibration", read_only=True, idempotent=True)
    async def scope_read_calibration() -> dict[str, Any]:
        """Per channel: offset PWM centres and gain factors per range, core A/B compensation, DC shift values,
        and derived volts per code for every range at 1x.

        Calibration is done on the scope's UI; these values are read-only from here. If volts look wrong, run
        the scope's base and signal calibration first."""
        def go() -> dict[str, Any]:
            s = sess.get_scope()
            out = []
            for ch in (0, 1):
                c = s.get_cal(ch, cached=False)
                vpl = [_r6(P.uv_per_lsb_q8(c.input_calibration[r], r) / 256 * 1e-6) for r in range(7)]
                out.append({"channel": CHANNEL_NAMES[ch], "dc_calibration_offset": list(c.dc_calibration_offset),
                            "input_calibration": list(c.input_calibration), "adc_a_comp": c.adc_a_comp,
                            "adc_b_comp": c.adc_b_comp, "dc_shift_center": c.dc_shift_center,
                            "dc_shift_size": c.dc_shift_size, "dc_shift_value": c.dc_shift_value,
                            "volts_per_lsb_1x_per_range": vpl,
                            "volts_per_div_1x_per_range": list(P.RANGE_VOLTS_PER_DIV)})
            return {"ok": True, "channels": out, "caveats": [
                "Calibration is done on the scope's UI; these values are read-only from here.", SCALING_CAVEAT]}
        return await run("scope_read_calibration", 0, {}, go)

    @register(2, title="Calibration wizard zero probe (diagnostic)")
    async def scope_cal_probe(
        channel: Annotated[Literal["CH1", "CH2"], Field(description="channel")] = "CH1",
        range_index: Annotated[int, Field(ge=0, le=5, description="hardware range 0..5 (0 = 5 V/div, the wizard's "
                                                                   "'range 1'; 5 = 0.1 V/div)")] = 0,
        whole_step: Annotated[bool, Field(description="run the wizard's whole zero step on the screen (both "
                                                      "channels, every range, about 15 s; the answer is this "
                                                      "channel's failing or last range)")] = False,
        settle_reads: Annotated[int, Field(ge=0, le=15, description="A/B test of the bus readout: extra data-port "
                                                                     "reads before each sampled read")] = 0,
        prestrobe: Annotated[bool, Field(description="A/B test: a data-write strobe before each sample strobe")] = False,
    ) -> dict[str, Any]:
        """Diagnostic (firmware 0.2.67+, protocol 1.21, provisional): the scope runs its calibration wizard's zero
        measurement of one channel and range and returns what the wizard saw: the verdict (OK, CLIP, NOISE, ZERO,
        SPREAD, SLOPE, DRIFT, CLASS, TIMING, READOUT), the failing point, class and core, and per point (coarse, gated
        slope and zero, full-rate zero and slope) per core the mean, RMS, extremes and clipped samples. Nothing is
        stored; the relays click and the scope's settings come back afterwards. Needs the inputs at 0 V (probes off
        or shorted) like the wizard. The scope's own acquisition is stopped for it (STOP; RUN is restored after)."""
        args = {"channel": channel, "range_index": range_index, "whole_step": whole_step,
                "settle_reads": settle_reads, "prestrobe": prestrobe}

        def go() -> dict[str, Any]:
            s = sess.get_scope()
            if s.info.proto_minor < 21:
                raise ToolError(f"scope_cal_probe needs firmware 0.2.67 or later (protocol 1.21; this scope speaks "
                                f"1.{s.info.proto_minor})")
            notes = sess.ensure_control()
            was_run = bool(s.shared and s.status().run_state == P.RunState.RUN)
            if was_run:
                s.set_acq_state(False)
            sess.relay_slot()
            try:
                r = s.cal_probe(CHANNEL_NAMES.index(channel), range_index, settle=settle_reads,
                                prestrobe=int(prestrobe), mode=int(whole_step))
            finally:
                if was_run:
                    s.set_acq_state(True)
            d = r.describe()
            cav = list(notes) + ["CAL_PROBE is a diagnostic of protocol 1.21 (provisional numbering): the layout may "
                                 "still change"]
            if was_run:
                cav.append("the scope's acquisition was stopped for the probe and set running again")
            return {"ok": True, **d, "caveats": cav}
        return await run("scope_cal_probe", 2, args, go)

    @register(0, title="Voltage scaling in effect", read_only=True, idempotent=True)
    async def scope_calibration_info() -> dict[str, Any]:
        """Which code-to-volts conversion is in effect per channel (the scope's calibration v3, the per-unit
        calibration file or the firmware formula), the scope's own calibration state (protocol 1.12: version 1 or
        3, flags, the correction for the current settings, and the stored per-core zeros, slopes and noise per clock
        class with the per-core, per-class gain corrections, the range and lever they were measured with), the calibration file and its fitted numbers: per range and ADC core the raw 0 V code at the
        reference offset, the offset-PWM slope, volts per code and the fit residual, plus the per-core channel
        model (0 V code at dcoffset 0, slope). Also the live conversion for each channel's current range and offset.
        Read-only; scope_calibrate_volts adds points."""
        def go() -> dict[str, Any]:
            s = sess.get_scope()
            cal = s.unit_cal
            live = {}
            for ch in (0, 1):
                cfg = s.get_channel(ch, cached=False)
                sc = s.scale(ch, cfg)
                live[CHANNEL_NAMES[ch]] = {
                    "scaling": sc.source, "range_index": cfg.range, "offset_pwm": cfg.offset_pwm,
                    "zero_raw": {"A": _r6(sc.zero_a), "B": _r6(sc.zero_b)},
                    "volts_per_lsb": {"A": _r6(sc.gain_a), "B": _r6(sc.gain_b)},
                    "zero_code_compensated": _r6(sc.zero_compensated),
                    "firmware_zero_code": _r6(cfg.zero_code), "firmware_volts_per_lsb": _r6(cfg.volts_per_lsb)}
            cals = [s.get_cal(ch, cached=False) for ch in (0, 1)]
            scope_cal = {CHANNEL_NAMES[ch]: _scope_cal_summary(cals[ch]) for ch in (0, 1)}
            out: dict[str, Any] = {"ok": True, "calibration_file": str(sess.cal_path()),
                                   "loaded": cal is not None, "live": live, "scope_calibration": scope_cal}
            if cals[0].has_provenance:
                out["calibration_provenance"] = _provenance_summary(cals[0], s.info)
            if cals[0].has_capture:  # protocol 1.24: the stored FPGA 0x153A capture stages
                out["capture_stages"] = cals[0].describe_capture()
            if cal is not None:
                out.update({
                    "unit": cal.unit, "points": len(cal.points),
                    "per_range": {CHANNEL_NAMES[ch]: {str(r): {c: f.to_dict() for c, f in cores.items()}
                                                      for r, cores in sorted(rr.items())}
                                  for ch, rr in sorted(cal.fit.items())},
                    "channel_model": {CHANNEL_NAMES[ch]: v for ch, v in sorted(cal.channel_fit.items())},
                    "summary": describe_cal(cal)})
            out["caveats"] = [SCALING_CAVEAT, CAL_OUTPUT_NOTE]
            if any(v.get("in_use") == "v3" for v in scope_cal.values()):
                out["caveats"].insert(0, "the scope's calibration v3 sets the 0 V codes and the per-core, per-class "
                                         "gains (READ_TRACE is already corrected; raw reads get the stored zeros and "
                                         "gains); the per-unit file only contributes its gated gains")
            return out
        return await run("scope_calibration_info", 0, {}, go)

    @register(0, title="Device USB event log", read_only=True)
    async def scope_event_log(
        last: Annotated[int, Field(ge=1, le=128, description="newest events to return")] = 64,
        fpga_trace: Annotated[bool | None, Field(description=(
            "protocol 1.22 (fw 0.2.72): switch the FPGA write trace (event type 22: every rate / filter write and "
            "sampling reset with its caller) on or off before reading; omit to leave it. It is off after boot because "
            "it floods the ring; the writes are always counted in GET_LINK_STATS 64-66"))] = None,
    ) -> dict[str, Any]:
        """The scope firmware's ring of the last 64 (128 from fw 0.2.15) USB / link events (protocol >= 1.5): bus
        resets, suspends/resumes, disconnects, EP0 setups, DTR changes, RX ring full/release, TX frames flushed or
        aborted, and lost USB interrupts served by the timer poll, each with the device time in ms. Use it to
        diagnose a link drop right after the scope reappears (a host port reset shows as suspend then bus reset).
        fw 0.2.15 adds reset_snapshot: the events right before the first bus reset since the snapshot was armed.
        fw 0.2.24 adds bus_events: the last 16 bus-level events (resets, suspend/resume, disconnect, configuration,
        halts, TX flush/abort with their EP2 state, restarts, USB init state), which EP0 setups and DTR edges cannot
        evict. This tool reads plainly, which keeps it on fw 0.2.18+ (0.2.15-0.2.17 re-armed on every read). Read-only."""
        def go() -> dict[str, Any]:
            s = sess.get_scope()
            log = s.event_log(fpga_trace=fpga_trace)
            if log is None:
                raise ToolError("scope_event_log: needs protocol 1.5 (firmware >= 0.2.12)")
            ev = log.events[-last:]
            def rows(evs):
                return [{"ms": e.ms, "ago_ms": log.now_ms - e.ms, "type": e.name, "detail": e.describe()} for e in evs]
            out = {"ok": True, "device_now_ms": log.now_ms, "total_since_boot": log.total, "events": rows(ev),
                   "caveats": []}
            if s.info.proto_minor >= 22:
                out["fpga_write_trace"] = log.fpga_trace
            if log.has_snapshot:
                out["reset_snapshot"] = ({"reset_ms": log.reset_ms, "events_before": rows(log.snapshot)}
                                         if log.frozen else None)
            if log.has_bus_ring:
                out["bus_events"] = rows(log.bus_events)
                out["bus_total_since_boot"] = log.bus_total
            return out
        return await run("scope_event_log", 0, {"last": last, "fpga_trace": fpga_trace}, go)

    @register(0, title="Read the scope's spectrum", read_only=True)
    async def scope_spectrum(
        channel: Annotated[Literal["CH1", "CH2"], Field(description="channel")] = "CH1",
        fmin: Annotated[float | str, Field(description="lowest frequency for peaks and returned bins, Hz")] = 0.0,
        fmax: Annotated[float | str | None, Field(description="highest frequency, Hz (default fs/2)")] = None,
        peaks: Annotated[int, Field(ge=1, le=100, description="number of peaks to list")] = 10,
        cross_check: Annotated[bool, Field(description="also read the same capture's trace and recompute the "
                                                       "spectrum on the host (on-scope FFT model and the "
                                                       "scope_capture_and_fft computation); about 0.1 s more")] = False,
    ) -> dict[str, Any]:
        """The spectrum the scope's own spectrum view shows (firmware >= 0.2.28, protocol 1.7): the on-scope FFT
        of the latest capture (Hann, flat-top or rect window as chosen at the scope; N = the largest power of two
        the record holds, up to the view's FFT length), with sample rate, bin width, window, ENBW / RBW, the volts
        per code the scope applied, the peaks (frequency, dBV, dBc) and the median noise floor. The full spectrum is
        saved as .npz (f, level_db). Read-only and cheap; it takes no capture (use scope_capture_and_fft for host
        averaging or other windows). Fails with a hint when the screen is not in the spectrum view or shows no
        spectrum of that channel (scope_status display_mode says which)."""
        def go() -> dict[str, Any]:
            s = sess.get_scope()
            if s.info.proto_minor < 7:
                raise ToolError("scope_spectrum: needs protocol 1.7 (firmware >= 0.2.28 with the spectrum view)")
            try:
                sp = s.read_spectrum(channel)
            except DeviceError as e:
                if e.status == P.Status.E_STATE:
                    st = s.status()
                    where = ("the screen is in the scope view: open main menu > Spectrum analyzer at the scope"
                             if st.display_mode not in (1, 2, 3) else
                             f"the spectrum view shows no spectrum of {channel} (channel off or not selected in the "
                             "view, or no capture yet)")
                    raise ToolError(f"scope_spectrum: {where}") from e
                raise
            h = sp.header
            f = sp.freqs
            lv = sp.db
            fs = sp.sample_rate
            f_lo = parse_quantity(fmin)
            f_hi = parse_quantity(fmax) if fmax is not None else fs / 2
            band = (f >= f_lo) & (f <= f_hi)
            amp = np.where(np.isnan(lv), 0.0, 10 ** (np.nan_to_num(lv, nan=-400.0) / 20))
            inband = amp[band & (np.arange(f.size) > 0)]
            floor = float(np.median(inband)) if inband.size else 0.0
            pk = A.peaks(f, amp, peaks, f_lo, f_hi, floor=floor * 3)
            ref = pk[0][1] if pk else 1.0
            unit = sp.unit
            plist = [{"frequency": _r6(fr), unit.lower(): _r6(A.db(a)), "dbc": _r6(A.db(a, ref))} for fr, a, _ in pk]
            path = sess.save_npz("spectrum", {"f": f, "level_db": lv},
                                 {"kind": "spectrum", "source": "READ_SPECTRUM", "channel": channel, "unit": unit,
                                  **h.to_dict()})
            out: dict[str, Any] = {
                "ok": True, "channel": channel, "unit": unit, "frame": h.frame, "sample_rate": _r6(fs),
                "divider": h.divider, "n_fft": sp.n_fft, "bin_width": _r6(sp.bin_width), "window": h.window_name,
                "enbw_bins": sp.enbw_bins, "rbw_hz": _r6(sp.enbw_bins * sp.bin_width),
                "gain_db": h.gain_cdb / 100, "bins": h.bins, "peaks": plist,
                "noise_floor_db_per_bin": _r6(A.db(floor)) if floor > 0 else None, "file": path}
            if int(band.sum()) <= sess.cfg.inline_max_points:
                out["spectrum"] = {"f": A._r(f[band]), "level_db": [None if np.isnan(x) else round(float(x), 2)
                                                                    for x in lv[band]]}
            cav = ["8-bit ADC: SFDR around 45-50 dBc at best; spurs below that are not trustworthy.",
                   "levels use the firmware's volts per code (gain_db); the per-unit calibration is not applied",
                   "a host capture with a raw divider is labelled with the UI time base's rate (known firmware limit)"]
            if cross_check:
                out["cross_check"] = _spectrum_cross_check(s, channel, sp)
                cav += out["cross_check"]["notes"]
            out["caveats"] = cav
            return out
        return await run("scope_spectrum", 0, {"channel": channel, "cross_check": cross_check}, go)

    @register(0, title="SD card info", read_only=True)
    async def scope_sd_info(
        verify: Annotated[bool, Field(description="recompute the eGON checksums of the images on the card (reads "
                                                  "them; a few hundred ms)")] = False,
        identify: Annotated[bool, Field(description="protocol 1.16: identify the image the card boots (version, "
                                                    "commit, whether it is the running image); cached by the scope "
                                                    "after the first read")] = True,
    ) -> dict[str, Any]:
        """The scope's SD card through the USB protocol (firmware >= 0.2.2, capability SD_SECTORS): size, partition
        start, the firmware's write window, and the eGON boot headers at sectors 16 (bootloader), 80 and 750 (the
        scope image): magic, length, stored checksum and, with verify, whether it matches. From firmware 0.2.52
        (protocol 1.16) also ``card_image``: what the card boots (version, commit hash and time, protocol) and
        whether that is the image running now (a FEL-loaded build differs). Read-only. There is no SD write tool:
        persisting firmware is a bench action (fnirsi1013d-flash --persist, fnirsi1013d-sd)."""
        def go() -> dict[str, Any]:
            s = sess.get_scope()
            if not s.profile().has(P.Caps.SD_SECTORS):
                raise ToolError("scope_sd_info: this firmware has no SD sector commands (capability SD_SECTORS, "
                                "firmware >= 0.2.2)")
            info = s.sd_info(verify, identify)
            names = {0: "none", 1: "eGON.BT0", 2: "eGON.EXE"}
            card_image = None
            if info.ident is not None and info.ident.state:
                ci, run_i = info.ident, s.info
                running = f"fw {run_i.fw_version_str} build {run_i.fw_build:08x}"
                card_image = {"state": P.SD_IDENT_STATES.get(ci.state, ci.state),
                              "version": ci.version_str or None, "build": f"{ci.build:08x}" if ci.build else None,
                              "commit_time": ci.time_str,
                              "protocol": f"{ci.proto_major}.{ci.proto_minor}" if ci.proto_minor else None,
                              "is_running_image": ci.running, "identified_ms": ci.ident_ms,
                              "text": (f"the card boots {ci.describe()}; " +
                                       ("that is the running image" if ci.running else f"running now: {running} "
                                        "(loaded from RAM / FEL or a different build)"))}
            checks = {0: "not checked", 1: "ok", 2: "bad", 3: "length invalid"}
            return {"ok": True, "card_sectors": info.card_sectors, "card_mib": _r6(info.card_sectors / 2048),
                    "partition_start": info.part_start or None,
                    "write_window": [16, info.write_end] if info.write_end else None,
                    "headers": [{"sector": h.sector, "magic": names.get(h.magic, h.magic),
                                 "length": h.length if h.magic else None,
                                 "checksum": f"0x{h.checksum:08X}" if h.magic else None,
                                 "check": checks.get(h.check, h.check)} for h in info.headers],
                    "card_image": card_image,
                    "caveats": ["read-only; writing the card is done with fnirsi1013d-flash --persist on the bench"]}
        return await run("scope_sd_info", 0, {"verify": verify, "identify": identify}, go)

    @register(0, title="Scope health", read_only=True)
    async def scope_health() -> dict[str, Any]:
        """The scope's own health (firmware 0.2.52+, protocol 1.16; answers even while a menu is open): USB state
        now (configured, suspended, reconnecting, high speed, port open), the CPU's MMU / cache state, the unit's
        life (power-on number, operating hours, uptime, how the previous power-on ended), whether the settings are
        saved, and the stored restart record (an exception that restarted the firmware; it survives power cycles
        until cleared at the scope); from protocol 1.19 (fw 0.2.62) ``encode_check``: how 200 MSa/s captures are
        checked for a garbled ADC core (record data on AL3 0x1532..0x1537, the FPGA's self-check 0x33 on 0x1538+),
        the last 0x33 byte with the failing pins, captures checked / found garbled / re-taken clean; from protocol
        1.24 (fw 0.2.79, FPGA 0x153A) ``capture_stages``: the per-core ADC capture stages in effect (trained or the
        power-on default), the last outlier counts and the cores the self-check flagged. Returns the
        values and ``in_words``. Use it first when the scope restarted, the link dropped, settings seem lost or a
        full-rate capture looks garbled on one core, and include it in a fault report."""
        def go() -> dict[str, Any]:
            s = sess.get_scope()
            h = s.health()
            if h is None:
                raise ToolError("scope_health needs firmware 0.2.52 or later (protocol 1.16); use scope_status and "
                                "scope_event_log on this firmware")
            d = h.describe()
            return {"ok": True, **d, "in_words": _health_words(d, h)}
        return await run("scope_health", 0, {}, go)

    @register(0, title="Report: plot and HTML page of a result")
    async def scope_report(
        source: Annotated[str, Field(description="'latest' (the newest result this server saved) or the .npz path a "
                                                 "tool returned (files.npz / file / path); a path relative to the "
                                                 "data directory works too")] = "latest",
        title: Annotated[str | None, Field(description="page and plot title (default: the kind and file name)")] = None,
        command: Annotated[str | None, Field(description="the command that produced the result, shown on the page "
                                                         "(default: the tool and the arguments recorded in the "
                                                         "file)")] = None,
        caveats: Annotated[list[str] | None, Field(description="caveats to add (the tool's own recorded caveats "
                                                               "are included)")] = None,
        plot: Annotated[bool, Field(description="draw the PNG (needs matplotlib)")] = True,
    ) -> dict[str, Any]:
        """Present a saved result to the person: a PNG plot and a self-contained HTML page (the plot embedded, the
        result table, the caveats and the generating command; no scripts or CDNs, works offline, light and dark).
        Works for scope_capture / scope_read_capture, scope_averaged_capture, scope_decay_fit,
        scope_capture_segments, scope_capture_and_fft, scope_spectrum and scope_bode_sweep (host or device) results.
        Files go to <home>/reports/<date>/ (FNIRSI_MCP_HOME, default ~/.fnirsi1013d). Without matplotlib (pip
        install "fnirsi1013d[plot]") there is no PNG and the page carries the numbers only. Touches no scope.

        Showing it: Claude Code renders the PNG when you Read the file; the HTML page can be published with the
        Artifact tool (or opened in a browser). Pass the caveats on with the numbers."""
        args = {"source": source, "title": title, "command": command, "caveats": caveats, "plot": plot}

        def go() -> dict[str, Any]:
            from . import report as R

            try:
                if source == "latest":
                    src = R.latest_result(cfg.data_dir)
                else:
                    src = Path(source).expanduser()
                    if not src.is_absolute() and not src.is_file():
                        src = Path(cfg.data_dir) / src
                res = R.build_report(src, title=title, command=command, caveats=caveats,
                                     out_dir=user_home() / "reports" / _dt.date.today().isoformat(), plot=plot)
            except R.ReportError as e:
                raise ToolError(f"scope_report: {e}") from None
            show = ["Claude Code: Read the PNG file to see the plot (and show it to the person)",
                    "publish the HTML page with the Artifact tool, or give the person its path to open in a browser"]
            if res["png"] is None:
                show = show[1:]
            return {"ok": True, "kind": res["kind"], "title": res["title"], "source": res["source"],
                    "png": res["png"], "html": res["html"], "summary": res["summary"],
                    "table": res["rows"][:50], "caveats": res["caveats"], "command": res["command"],
                    "plot_note": res["plot_note"], "how_to_show": show}
        return await run("scope_report", 0, args, go)

    @register(1, title="Save a diagnostics report on the scope")
    async def scope_save_report() -> dict[str, Any]:
        """Ask the scope to write its diagnostics report to its SD card (firmware 0.2.52+, protocol 1.16, cap
        SD_SECTORS): reports/report-PPPPP-NN.txt on the card's FAT partition (PPPPP = power-on number), with the
        identity of the running and the card image, unit life, USB state, battery, restart counters and the stored
        restart record, every link counter by name, the decoded event log and both channels' calibration in words.
        The same file as the Diagnostics "Save report" button. Returns the file name and size; the person reads it
        from the card (USB mass-storage mode or a card reader). Changes no settings. A menu open on the scope is closed
        for it (firmware 0.2.78+, protocol 1.23); older firmware fails with E_BUSY until the person closes it."""
        def go() -> dict[str, Any]:
            s = sess.get_scope()
            if not s.profile().has(P.Caps.SD_SECTORS) or s.info.proto_minor < 16:
                raise ToolError("scope_save_report needs firmware 0.2.52 or later (protocol 1.16) with the SD "
                                "commands")
            try:
                r = s.save_report()
            except DeviceError as e:
                if e.status == P.Status.E_IO:
                    why = {0: "no SD card", 3: "the card is not ready", 7: "access denied (card write-protected?)",
                           8: "100 reports exist for this power-on: power-cycle or delete some"}.get(
                               e.detail, f"FatFs error {e.detail}")
                    raise ToolError(f"scope_save_report: the scope could not write the report: {why}") from e
                raise
            return {"ok": True, "file": r.name_str, "bytes": r.bytes_written,
                    "applied": {"file": r.name_str},
                    "caveats": ["the report is on the scope's SD card (FAT partition), not on this computer: read it "
                                "through the scope's USB mass-storage mode or a card reader"]}
        return await run("scope_save_report", 1, {}, go)

    @register(0, title="Screenshot", read_only=True)
    async def scope_screenshot() -> Image:
        """PNG image of the scope's 800x480 screen. Only on firmware that reports the SCREENSHOT capability
        (check scope_info caps first): firmware 0.2.70 and later have READ_SCREEN; older builds do not, and this
        tool then fails at once with "no SCREENSHOT capability". 768,000 bytes in 12 requests, about 1 s over USB.
        A menu open on the scope is closed first (firmware 0.2.78+, protocol 1.23), so the picture shows the base
        screen; older firmware fails with E_BUSY while a menu is open. Pixel format RGB565 is unverified on the unit
        (T9): if colours look wrong, say so."""
        def go() -> bytes:
            s = sess.get_scope()
            if not s.profile().has(P.Caps.SCREENSHOT):
                raise ToolError("scope_screenshot: this firmware build has no SCREENSHOT capability (READ_SCREEN is "
                                "not built in); use scope_fetch_waveform to see what the scope shows")
            return _png_from_rgb565(s.read_screen())
        data = await run("scope_screenshot", 0, {}, go)
        return Image(data=data, format="png")

    # ---- tier 2: channel ---------------------------------------------------------------------------------

    @register(2, title="Configure a channel")
    async def scope_set_channel(
        channel: Annotated[Literal["CH1", "CH2"], Field(description="channel to configure")],
        enabled: Annotated[bool | None, Field(description="readout/display only; both channels are always "
                                                          "sampled")] = None,
        volts_per_div: Annotated[float | str | None, Field(description="V/div at the probe tip (after the probe "
                                 "factor), snapped to the nearest range, e.g. 0.1 or '100mV'")] = None,
        range_index: Annotated[int | None, Field(ge=0, le=6, description="raw range 0..6 = 5 V..50 mV/div at 1x; "
                                                                         "alternative to volts_per_div")] = None,
        coupling: Annotated[Literal["DC", "AC"] | None, Field(description="input coupling relay")] = None,
        offset_div: Annotated[float | None, Field(description="vertical offset in divisions, positive moves the "
                                                              "trace up (UI semantics)")] = None,
        offset_raw: Annotated[int | None, Field(ge=0, le=2000, description="raw offset PWM compare value "
                                                                           "0..2000; alternative to offset_div")] = None,
        probe: Annotated[float | None, Field(description="probe factor 0.5, 1, 10, 20, 50, 100 or 1000; scaling "
                                                         "only, never changes hardware")] = None,
        invert: Annotated[bool | None, Field(description="invert the channel")] = None,
    ) -> dict[str, Any]:
        """Configure one input channel: range (V/div), coupling, offset, probe factor, invert, enable.

        Changing volts_per_div or coupling clicks relays and needs 50 ms settling (handled); the server limits
        relay changes to 5 per second. 50 mV/div is a software zoom of 100 mV/div: same resolution, about 3.4 mV
        per code. Offset is a PWM-derived DC injection; after changing it, wait for one capture before trusting
        DC levels. Returns the applied range, volts per code, full-scale span in volts and the zero code."""
        args = {k: v for k, v in dict(channel=channel, enabled=enabled, volts_per_div=volts_per_div,
                                      range_index=range_index, coupling=coupling, offset_div=offset_div,
                                      offset_raw=offset_raw, probe=probe, invert=invert).items() if v is not None}

        def go() -> dict[str, Any]:
            notes = sess.ensure_control()
            s = sess.get_scope()
            ch = channel_index(channel)
            cur = s.get_channel(ch, cached=False)
            cal = s.get_cal(ch)
            probe_index = None
            if probe is not None:
                try:
                    probe_index = P.PROBE_FACTORS.index(float(probe))
                except ValueError:
                    raise ValueError(f"probe {probe} not one of {list(P.PROBE_FACTORS)}") from None
            pf = P.PROBE_FACTORS[probe_index] if probe_index is not None else cur.probe_factor
            rng = range_index
            if volts_per_div is not None:
                if range_index is not None:
                    raise ValueError("give volts_per_div or range_index, not both")
                want = parse_quantity(volts_per_div, "V")
                if want <= 0:
                    raise ValueError("volts_per_div must be > 0")
                rng = int(np.argmin([abs(math.log(_volts_per_div_at_probe(r, pf) / want)) for r in range(7)]))
            new_rng = cur.range if rng is None else rng
            dcoffset = None
            if offset_div is not None and offset_raw is not None:
                raise ValueError("give offset_div or offset_raw, not both")
            if offset_div is not None:
                vpl = P.uv_per_lsb_q8(cal.input_calibration[new_rng], new_rng) / 256 * 1e-6
                codes_per_div = P.RANGE_VOLTS_PER_DIV[new_rng] / vpl
                shift = offset_div * codes_per_div
                dcoffset = int(round(-shift * (cal.dc_shift_value or 1) / 100))
                center = cal.dc_calibration_offset[new_rng]
                if not 0 <= center + dcoffset <= 2000:
                    per_div = codes_per_div * 100 / (cal.dc_shift_value or 1)
                    lo, hi = -(2000 - center) / per_div, center / per_div
                    raise ValueError(f"offset_div {offset_div:g} is out of reach on this range: the offset PWM "
                                     f"allows about {lo:.2f} .. {hi:.2f} divisions [U scaling]. Use a smaller "
                                     "offset or a larger volts_per_div")
            relay = (rng is not None and min(rng, 5) != min(cur.range, 5)) or \
                    (coupling is not None and int(coupling == "AC") != cur.coupling)
            waited = sess.relay_slot() if relay else 0.0
            s.set_channel(ch, enable=enabled, range_index=rng,
                          coupling=None if coupling is None else int(coupling == "AC"), dcoffset=dcoffset,
                          offset_pwm=offset_raw, invert=invert, probe_index=probe_index)
            summ = _channel_summary(s, ch)
            cav = list(notes)
            if summ["software_zoom"]:
                cav.append("50 mV/div is a software zoom of 100 mV/div: same ~3.4 mV per code resolution")
            if dcoffset is not None or offset_raw is not None:
                cav.append("offset is a PWM-derived DC injection: wait for one capture before trusting DC levels")
            if volts_per_div is not None:
                want = parse_quantity(volts_per_div, "V")
                if abs(summ["volts_per_div"] - want) / want > 0.05:
                    cav.append(f"requested {want:g} V/div, nearest available is {summ['volts_per_div']:g} V/div")
            if waited:
                cav.append(f"relay rate limit: waited {waited:.2f} s")
            return {"ok": True, "applied": summ, "relay_switched": relay, "caveats": cav + [SCALING_CAVEAT]}
        return await run("scope_set_channel", 2, args, go)

    # ---- tier 1: timebase / trigger ------------------------------------------------------------------------

    @register(1, title="Set the sample rate", idempotent=True)
    async def scope_set_timebase(
        sample_rate: Annotated[float | str | None, Field(description="samples/s per channel; snapped to "
                               "200 MHz / (N + 1), nearest at or above the request, e.g. 20e6 or '20MS/s'")] = None,
        window: Annotated[float | str | None, Field(description="capture duration in seconds: chooses the highest "
                                                                "rate that fits it into total_samples, e.g. '2ms'")] = None,
        divider: Annotated[int | None, Field(ge=0, description="raw divider N, fs = 200 MHz / (N + 1)")] = None,
    ) -> dict[str, Any]:
        """Set the sample rate (one of sample_rate, window, divider). Returns the actual rate, dt and the capture
        window = total_samples x dt.

        The trigger is checked only every 2 samples (core A), so trigger jitter is up to 2 / fs. For averaging
        of PI decays, prefer >= 10 MS/s so the 2-sample jitter (200 ns) stays small against the decay. Roll /
        long timebases (>= 20 ms/div in the UI) are not available remotely in protocol v1."""
        args = {k: v for k, v in dict(sample_rate=sample_rate, window=window, divider=divider).items()
                if v is not None}

        def go() -> dict[str, Any]:
            if len(args) != 1:
                raise ValueError("give exactly one of sample_rate, window or divider")
            notes = sess.ensure_control()
            s = sess.get_scope()
            if divider is not None:
                n = divider
            elif sample_rate is not None:
                fs = parse_quantity(sample_rate)
                if fs <= 0:
                    raise ValueError("sample_rate must be > 0")
                n = max(0, int(math.floor(P.BASE_CLOCK_HZ / fs + 1e-9)) - 1)
            else:
                w = parse_quantity(window, "s")  # type: ignore[arg-type]
                total = s.get_trigger().total_samples
                n = max(0, int(math.ceil(w * P.BASE_CLOCK_HZ / total - 1e-9)) - 1)
            s.set_timebase(divider=n)
            summ = _timebase_summary(s)
            cav = list(notes) + ["trigger jitter up to 2 / fs = " + fmt_si(2 * summ["dt"], "s")]
            if summ["sample_rate"] < 10e6:
                cav.append("below 10 MS/s the 2-sample trigger jitter may be significant against fast decays; "
                           "use align='edge' when averaging")
            return {"ok": True, "applied": summ, "caveats": cav}
        return await run("scope_set_timebase", 1, args, go)

    @register(1, title="Set the decimation filter", idempotent=True)
    async def scope_set_decimation(
        filter: Annotated[Literal["off", "boxcar", "boxcar+fir", "cic2", "cic2+fir", "cic3", "cic3+fir"], Field(
            description="off = legacy 8-bit sample skipping; boxcar = mean of N raw samples; cic2 / cic3 = 2- / "
                        "3-fold boxcar (better alias rejection, slightly less noise reduction per N); +fir (FPGA "
                        "0x1535+, caps FIR) adds the CIC compensation FIR: flat passband to fs/4 and about 20 dB "
                        "more alias rejection, active at 10 MSa/s and slower")],
    ) -> dict[str, Any]:
        """The scope's decimation filter (protocol 1.9, FPGA 0x1534+, capability DECIM). At sample rates below
        200 MSa/s the FPGA filters every raw 200 MSa/s sample instead of skipping, N = 200 MSa/s / output rate, and
        stores 16-bit samples: anti-aliased and up to about 8 + log2(sqrt N) bits (noise permitting). Returns the
        effective filter (the firmware lowers the order where it would not be exact: CIC3 only up to N = 10,334,
        CIC2 up to 1,050,630; 200 MSa/s and totals over 8190 stay legacy), N, the output shift S, the resolution in
        codes per stored count and an effective-bits estimate. The +fir choices (protocol 1.11, FPGA 0x1535) run
        the CIC on N / 2 and a 15-tap compensation FIR decimating by 2 (only where N >= 16 and N % 4 == 0; the
        result's effective filter says whether it engaged). Needs scope_set_trigger total_samples <= 8190 first.
        Captures and fetches then read the 16-bit samples automatically (the .npz gets CH<n>_samples16). For decay
        curves: cic2 or boxcar at the rate the decay needs; see the measurement guide."""
        args = {"filter": filter}

        def go() -> dict[str, Any]:
            notes = sess.ensure_control()
            s = sess.get_scope()
            if not s.profile().has(P.Caps.DECIM):
                raise ToolError("scope_set_decimation: this firmware / bitstream has no decimation filter "
                                "(capability DECIM: firmware >= 0.2.30 with FPGA 0x1534)")
            if filter.endswith("+fir") and not s.profile().has(P.Caps.FIR):
                raise ToolError("scope_set_decimation: this bitstream has no compensation FIR (capability FIR: "
                                "firmware >= 0.2.44 with FPGA 0x1535); use the filter without +fir")
            if filter != "off" and s.get_trigger(cached=False).total_samples > P.DECIM_MAX_TOTAL:
                raise ToolError(f"scope_set_decimation: the trigger total is over {P.DECIM_MAX_TOTAL} samples; "
                                f"scope_set_trigger(total_samples <= {P.DECIM_MAX_TOTAL}) first")
            st = s.set_decim(filter)
            summ = _decim_summary(st)
            cav = list(notes) + DECIM_CAVEATS
            if st.why not in (0, 1):
                cav.insert(0, f"effective filter is {summ['effective']}: {summ['reason']}")
            if "fir_note" in summ:
                cav.insert(0, summ["fir_note"])
            return {"ok": True, "applied": summ, "timebase": _timebase_summary(s), "caveats": cav}
        return await run("scope_set_decimation", 1, args, go)

    @register(1, title="Set the trigger and capture geometry", idempotent=True)
    async def scope_set_trigger(
        source: Annotated[Literal["CH1", "CH2", "EXT"] | None, Field(description="trigger channel; EXT = the "
                          "calibration pin as a 3.3 V logic input referenced to the USB ground, not the probe ground "
                          "(bitstream 0x1533+, caps EXT_TRIGGER, generator off; level ignored)")] = None,
        edge: Annotated[Literal["rising", "falling"] | None, Field(description="trigger edge")] = None,
        level: Annotated[float | str | None, Field(description="trigger level in volts at the probe tip, "
                                                               "converted to a raw core-A code")] = None,
        level_code: Annotated[int | None, Field(ge=0, le=255, description="raw core-A code 0..255; alternative "
                                                                          "to level")] = None,
        mode: Annotated[Literal["auto", "normal", "single"] | None, Field(description="host captures are always "
                        "single shots, so single = normal for them; the mode also drives the scope's own RUN "
                        "loop")] = None,
        pretrigger_fraction: Annotated[float | None, Field(ge=0, le=1, description="share of the capture before "
                                                                                   "the trigger")] = None,
        pretrigger_samples: Annotated[int | None, Field(ge=0, description="alternative to pretrigger_fraction "
                                                                          "(even)")] = None,
        total_samples: Annotated[int | None, Field(ge=2, description="samples per channel, even, 80 .. max for "
                                                                     "the bitstream (16382 on AL3)")] = None,
        auto_timeout: Annotated[float | str | None, Field(description="auto mode: seconds without a trigger "
                                                                      "before the capture completes anyway")] = None,
    ) -> dict[str, Any]:
        """Set trigger source, edge, level, mode and the capture geometry (pre-trigger and total samples).
        Returns applied values, the level actually reachable (codes are about 3.4 mV apart at 100 mV/div) and
        the pre/post trigger durations.

        Level is an 8-bit code compared to core A only; edges faster than 2 / fs may be missed. Hold-off is not
        implemented in any current FPGA bitstream. On the stock FPGA pre-trigger is fixed at 50 % and total at
        3000."""
        args = {k: v for k, v in dict(source=source, edge=edge, level=level, level_code=level_code, mode=mode,
                                      pretrigger_fraction=pretrigger_fraction, pretrigger_samples=pretrigger_samples,
                                      total_samples=total_samples, auto_timeout=auto_timeout).items() if v is not None}

        def go() -> dict[str, Any]:
            notes = sess.ensure_control()
            s = sess.get_scope()
            prof = s.profile()
            cur = s.get_trigger(cached=False)
            cav = list(notes)
            src = None if source is None else ("CH1", "CH2", "EXT").index(source)
            if src == 2 and not prof.has(P.Caps.EXT_TRIGGER):
                raise ValueError("source 'EXT' needs an FPGA bitstream with an external trigger input (0x1533 or "
                                 f"later; this one is 0x{prof.info.fpga_version:04X}). Trigger on CH2 instead.")
            gen_was_on = src == 2 and prof.has_generator and bool(s.get_gen().enable)
            eff_src = cur.source if src is None else src
            if eff_src == 2:
                cav.append(EXT_NOTE)
                if level is not None or level_code is not None:
                    cav.append("level is ignored for EXT (3.3 V logic input)")
            code = level_code
            if level is not None and eff_src <= 1:
                if level_code is not None:
                    raise ValueError("give level or level_code, not both")
                v = parse_quantity(level, "V")
                cfg = s.get_channel(eff_src, cached=False)
                raw = round(s.scale(eff_src, cfg).raw_a_for_volts(v))
                code = int(min(255, max(0, raw)))
                if raw != code:
                    cav.append(f"level {v:g} V is outside the channel's range; clamped to code {code}")
            total = total_samples
            pre = pretrigger_samples
            if pretrigger_fraction is not None and pre is not None:
                raise ValueError("give pretrigger_fraction or pretrigger_samples, not both")
            if total is not None or pre is not None or pretrigger_fraction is not None:
                t_eff = total if total is not None else cur.total_samples
                if pretrigger_fraction is not None:
                    pre = int(round(pretrigger_fraction * t_eff / 2)) * 2
                    pre = min(pre, t_eff - 2)
                if not prof.programmable:
                    if (total is not None and total != 3000) or (pre is not None and pre != 1500):
                        raise ValueError("the stock FPGA (0x1432) has a fixed geometry: pretrigger 1500, total "
                                         "3000. Flash the Atlan4 AL3 bitstream for programmable geometry.")
                i = prof.info
                if total is not None:
                    if total % 2:
                        raise ValueError(f"total_samples must be even (got {total})")
                    if not i.min_total_samples <= total <= i.max_total_samples:
                        raise ValueError(f"total_samples {total} not in {i.min_total_samples}.."
                                         f"{i.max_total_samples} for this FPGA ({prof.variant})")
                if pre is not None:
                    if pre % 2:
                        raise ValueError(f"pretrigger_samples must be even (got {pre})")
                    if pre > t_eff - 2:
                        raise ValueError(f"pretrigger_samples must be <= total_samples - 2 (got {pre}, total "
                                         f"{t_eff}). Lower pretrigger_fraction.")
            ato = None
            if auto_timeout is not None:
                secs = parse_quantity(auto_timeout, "s")
                ato = max(1, int(round(secs / (2 * s.get_timebase().dt))))
            s.set_trigger(source=src, edge=None if edge is None else P.EDGES.index(edge), level=code,
                          mode=None if mode is None else P.TRIG_MODES.index(mode), pretrigger_samples=pre,
                          total_samples=total, auto_timeout=ato)
            summ = _trigger_summary(s)
            if src == 2 and gen_was_on:
                cav.append("the generator was switched off: the calibration pin is now the EXT trigger input")
            if mode == "single":
                cav.append("host captures never re-arm on their own: 'single' behaves like 'normal' for them")
            cav += ["level is an 8-bit code compared to core A only; edges faster than 2 / fs may be missed",
                    "hold-off is not implemented in any current FPGA bitstream"]
            if not prof.programmable:
                cav.append("stock FPGA: pre-trigger fixed at 50 %, total 3000 samples")
            return {"ok": True, "applied": summ, "caveats": cav}
        return await run("scope_set_trigger", 1, args, go)

    # ---- tier 1: acquisition -------------------------------------------------------------------------------

    @register(1, title="Capture a waveform")
    async def scope_capture(
        channels: Annotated[list[str] | None, Field(description="e.g. ['CH1', 'CH2']; default: enabled "
                                                                "channels")] = None,
        timeout_s: Annotated[float, Field(gt=0, le=60, description="normal mode waits this long for a "
                                                                   "trigger")] = 2.0,
        window: Annotated[list[float | str] | None, Field(description="[t0, t1] seconds relative to the trigger; "
                                                                      "reads only this part (faster)")] = None,
        raw: Annotated[bool, Field(description="also store raw codes per ADC core in the file")] = False,
        processed: Annotated[bool, Field(description="use the firmware's processed trace (slower, matches the "
                                                     "screen) instead of raw reads plus host compensation")] = False,
    ) -> dict[str, Any]:
        """Take one host single capture with the live settings: arm, wait for the trigger, read. The scope
        screen shows it and the scope stays in STOP afterwards (resume with scope_acquisition_control(action=
        'run')). Returns per channel min, max, mean, rms, Vpp, frequency estimate and clipped-sample count; the
        trigger outcome ('fired', 'auto_timeout', 'none'), capture_id, settings_seq, dt, sample count, the .npz
        file path (t, volts, codes, clipped flags, metadata) and either the samples inline (<= 2000 points) or a
        decimated min/max preview.

        Dynamic caveats report clipping, unsynchronised auto-trigger data, likely aliasing and content above the
        analog bandwidth."""
        args = {"channels": channels, "timeout_s": timeout_s, "window": window, "raw": raw, "processed": processed}

        def go() -> dict[str, Any]:
            notes = sess.ensure_control()
            s = sess.get_scope()
            chans = _channels_arg(channels, [CHANNEL_NAMES[c] for c in (0, 1) if s.get_channel(c).enable] or ["CH1"])
            w = _window_arg(window)
            a, n = _sample_window(s, w)
            s.arm()
            st = s.wait(timeout_s)
            if st.acq_state != P.AcqState.DONE:
                s.stop()
                return {"ok": False, "trigger": "none", "acq_state": P.AcqState(st.acq_state).name,
                        "caveats": notes + [f"no trigger within {timeout_s} s: check the trigger level/source with "
                                            "scope_get_config, use mode='auto', or raise timeout_s"]}
            if processed:
                cap = s.read_trace_capture(chans)
                if w is not None:
                    cap = _slice_capture(cap, a, n)
            else:
                cap = s.read_capture(chans, a, n, keep_raw=raw, st=st)
            out = _capture_result(sess, cap, "capture")
            if s.shared:
                notes.append("the scope screen shows this capture and the scope is now in STOP; "
                             "scope_acquisition_control(action='run') resumes its own acquisition")
            out["caveats"] = notes + out["caveats"]
            return out
        return await run("scope_capture", 1, args, go)

    @register(0, title="Fetch the current waveform", read_only=True)
    async def scope_fetch_waveform(
        channels: Annotated[list[str] | None, Field(description="channels to read, default ['CH1']")] = None,
        window: Annotated[list[float | str] | None, Field(description="[t0, t1] seconds relative to the "
                                                                      "trigger")] = None,
        processed: Annotated[bool, Field(description="read the firmware's processed trace (what the screen "
                                                     "draws) instead of raw reads")] = False,
    ) -> dict[str, Any]:
        """Read the latest capture without taking a new one: what the scope screen shows. Collaborative
        firmware: the newest finished capture of any origin (the scope's RUN loop, a single at the scope, or a
        host capture), converted with the settings it was taken under (reported as capture_settings; they may
        differ from the live ones). Legacy firmware: the UI's last trace in LOCAL, the last remote capture in
        REMOTE. Tier 0: changes nothing (in RUN mode, if the capture keeps being replaced during a chunked read,
        RUN is paused for that one read and the caveats say so). Same result format as scope_capture."""
        args = {"channels": channels, "window": window, "processed": processed}

        def go() -> dict[str, Any]:
            s = sess.get_scope()
            chans = _channels_arg(channels, ["CH1"])
            st = s.status()
            w = _window_arg(window)
            if st.shared and not processed:
                if st.capture_id == 0:
                    raise ToolError("scope_fetch_waveform: no capture since the scope booted; press RUN on the "
                                    "scope or call scope_capture")
                cap = s.read_capture(chans)
                if w is not None:
                    a = max(0, int(math.floor(w[0] / cap.dt)) + cap.pretrigger_samples)
                    b = min(cap.total_samples, int(math.ceil(w[1] / cap.dt)) + cap.pretrigger_samples + 1)
                    cap = _slice_capture(cap, a, b - a)
            elif st.control != P.Control.REMOTE or processed:
                cap = s.read_trace_capture(chans)
                if w is not None:
                    tb = cap.dt
                    a = max(0, int(math.floor(w[0] / tb)) + cap.pretrigger_samples)
                    b = min(cap.total_samples, int(math.ceil(w[1] / tb)) + cap.pretrigger_samples + 1)
                    cap = _slice_capture(cap, a, b - a)
            else:
                if st.acq_state != P.AcqState.DONE:
                    raise ToolError("scope_fetch_waveform: no completed remote capture (state "
                                    f"{P.AcqState(st.acq_state).name}); call scope_capture first")
                a, n = _sample_window(s, w)
                cap = s.read_capture(chans, a, n, st=st)
            return _capture_result(sess, cap, "fetch")
        return await run("scope_fetch_waveform", 0, args, go)

    @register(1, title="Arm / wait / force / stop")
    async def scope_acquisition_control(
        action: Annotated[Literal["arm", "wait", "force", "stop", "run"], Field(
            description="arm a host single capture, wait for it, force it to end, stop (also stops the scope's RUN "
                        "loop), or run (resume the scope's own continuous acquisition)")],
        timeout_s: Annotated[float, Field(gt=0, le=10, description="for wait")] = 2.0,
    ) -> dict[str, Any]:
        """Low-level control for agents that want to arm, do something else (e.g. fire the detector), then wait
        and read with scope_fetch_waveform; and the scope's RUN/STOP state, shared with the person at the scope
        (either side can press RUN or STOP; 'run' needs collaborative firmware).

        'force' is only available once verified on this FPGA variant (capability FORCE_TRIGGER, bench test T7);
        otherwise use trigger mode 'auto'."""
        args = {"action": action, "timeout_s": timeout_s}

        def go() -> dict[str, Any]:
            notes = sess.ensure_control()
            s = sess.get_scope()
            if action == "arm":
                st = s.arm()
            elif action == "wait":
                st = s.wait(timeout_s)
            elif action == "force":
                if not s.profile().has(P.Caps.FORCE_TRIGGER):
                    raise ToolError("scope_acquisition_control: FORCE is not verified on this FPGA variant (T7); "
                                    "use scope_set_trigger(mode='auto') instead")
                st = s.force()
            elif action == "run":
                if not s.shared:
                    raise ToolError("scope_acquisition_control: 'run' needs collaborative firmware (SHARED_MODE); "
                                    "on this firmware the UI runs again when REMOTE is released")
                st = s.set_acq_state(True)
            else:
                st = s.stop()
            out = {"ok": True, "acq_state": P.AcqState(st.acq_state).name, "capture_id": st.capture_id,
                   "flags": [f.name for f in P.StatusFlags if st.flags & f], "applied": {"action": action},
                   "caveats": notes}
            if st.shared:
                out["run_state"] = P.RunState(st.run_state).name
            return out
        return await run("scope_acquisition_control", 1, args, go)

    # ---- tier 1: derived ---------------------------------------------------------------------------------

    async def _averaged(ctx: Context | None, *, n: int, chans: list[str], w: tuple[float, float] | None,
                        align: str, align_channel: str | None, align_level: float | None, align_edge: str | None,
                        timeout_s: float, reject_clipped: bool, on_device: bool | None,
                        tool: str, hardware: bool = False) -> dict[str, Any]:
        """Shared engine of averaged_capture / decay_fit / capture_and_fft-like loops. Holds the session lock
        across the whole run (other tools wait).

        Collaborative firmware: the run's settings are pinned to one settings_seq (``Scope.capture_context``).
        A capture taken under another settings_seq means someone changed a setting at the scope: the run starts
        over with the new settings (``restarts`` in the result says when and what changed), at most
        ``max_restarts`` times."""
        held = _Held(sess)
        try:
            await anyio.to_thread.run_sync(held.acquire)
            notes = await anyio.to_thread.run_sync(sess.ensure_control)
            s = sess.get_scope()
            restarts: list[dict[str, Any]] = []
            t0 = time.monotonic()
            while True:
                cctx = await anyio.to_thread.run_sync(s.capture_context)
                try:
                    res = await _averaged_run(ctx, s, cctx, n=n, chans=chans, w=w, align=align,
                                              align_channel=align_channel, align_level=align_level,
                                              align_edge=align_edge, timeout_s=timeout_s,
                                              reject_clipped=reject_clipped, on_device=on_device, tool=tool,
                                              notes=notes, hardware=hardware)
                except _SettingsChanged as sc:
                    new = await anyio.to_thread.run_sync(s.capture_context)
                    changed = settings_diff(cctx.settings_key(), new.settings_key())
                    restarts.append({"after_captures": sc.done, "settings_seq": [cctx.settings_seq, sc.seq],
                                     "changed": changed})
                    if len(restarts) > cfg.max_restarts:
                        raise ToolError(f"{tool}: settings were changed at the scope {len(restarts)} times during the "
                                        f"run (last change: {changed or 'unknown'}); ask the person at the scope to "
                                        "leave the settings alone, then retry") from None
                    if ctx is not None:
                        await _progress(ctx, 0, n, f"settings changed at the scope ({changed}); restarting")
                    continue
                res["restarts"] = restarts
                res["elapsed"] = time.monotonic() - t0
                if restarts:
                    what = "; ".join(", ".join(f"{k} {v[0]} -> {v[1]}" for k, v in r["changed"].items()) or "unknown"
                                     for r in restarts)
                    res["notes"] = res["notes"] + [
                        f"settings were changed at the scope during the run: restarted {len(restarts)} time(s) "
                        f"({what}); the result uses only captures taken with the final settings"]
                return res
        except BaseException as e:
            if isinstance(e, (anyio.get_cancelled_exc_class(), KeyboardInterrupt)):
                with anyio.CancelScope(shield=True):
                    try:
                        await anyio.to_thread.run_sync(lambda: sess.get_scope().stop())
                    except Exception:  # noqa: BLE001
                        pass
            if isinstance(e, TransportError):
                sess.drop_connection()
            raise
        finally:
            held.release()

    async def _averaged_run(ctx: Context | None, s: Scope, cctx: CaptureContext, *, n: int, chans: list[str],
                            w: tuple[float, float] | None, align: str, align_channel: str | None,
                            align_level: float | None, align_edge: str | None, timeout_s: float,
                            reject_clipped: bool, on_device: bool | None, tool: str,
                            notes: list[str], hardware: bool = False) -> dict[str, Any]:
        """One attempt of the averaging run with settings ``cctx``. Raises _SettingsChanged."""
        notes = list(notes)
        trig = cctx.trigger
        dt = cctx.timebase.dt
        pre = trig.pretrigger_samples
        a, cnt = _sample_window(s, w)
        prof = s.profile()
        shared = s.shared
        if hardware:
            return await _hw_averaged_run(s, cctx, n=n, chans=chans, a=a, cnt=cnt, align=align,
                                          timeout_s=timeout_s, reject_clipped=reject_clipped, tool=tool, notes=notes)
        dev_fits = 16 + 2 * cnt * len(chans) <= s.response_limit
        use_dev = (prof.has(P.Caps.DEVICE_AVERAGE) and align == "hardware" and on_device is not False
                   and not reject_clipped and dev_fits)
        if on_device and not prof.has(P.Caps.DEVICE_AVERAGE):
            notes.append("on_device requested but the firmware has no ACQUIRE_AVG; averaged on the host")
        if on_device and align != "hardware":
            notes.append("on_device averaging supports hardware alignment only; averaged on the host")
        if on_device and not dev_fits:
            notes.append("on_device: the averaged window does not fit one response; averaged on the host")
        if on_device and reject_clipped and align == "hardware" and prof.has(P.Caps.DEVICE_AVERAGE) and dev_fits:
            use_dev = True
            notes.append("on_device: clipped captures cannot be rejected on the device")
        if use_dev:
            means: dict[str, np.ndarray] = {}
            left = n
            used = dropped = 0
            per_ms = int(timeout_s * 1000)
            chunk = max(1, min(4096, 60000 // max(per_ms, 1)))
            acc: dict[int, np.ndarray] = {}
            while left > 0:
                k = min(left, chunk)
                hdr, data = await anyio.to_thread.run_sync(
                    lambda k=k: s.acquire_avg_device(k, chans, a, cnt, per_ms, trig.mode != 0))
                if shared:
                    st = await anyio.to_thread.run_sync(s.status)
                    if st.settings_seq != cctx.settings_seq or st.capture_settings_seq != cctx.settings_seq:
                        raise _SettingsChanged(used, st.settings_seq)
                for c, m in data.items():
                    acc[c] = acc.get(c, 0) + m * hdr.n_used
                used += hdr.n_used
                dropped += hdr.n_dropped
                left -= k
                if ctx is not None:
                    await _progress(ctx, n - left, n, f"{used} captures averaged on the device")
            if used == 0:
                raise ToolError(f"{tool}: no capture triggered within {timeout_s} s each ({dropped} dropped); "
                                "check the trigger with scope_get_config")
            for c, v in acc.items():
                means[CHANNEL_NAMES[c]] = s.scale(c, cctx.channels[c]).volts_mean_compensated(v / used)
            tt = (a + np.arange(cnt) - pre) * dt
            return {"mode": "device", "t": tt, "mean": means, "std": None, "clipped_any": None,
                    "n_used": used, "n_dropped": dropped, "drop_reasons": {"no_trigger": dropped},
                    "noise_single": None, "notes": notes, "dt": dt, "edge_shifts": None}
        # host averaging
        al_ch = channel_index(align_channel) if align_channel else (trig.source if trig.source < 2 else 0)
        al_name = CHANNEL_NAMES[al_ch]
        al_edge = align_edge or P.EDGES[trig.edge]
        if align == "edge":
            lvl = align_level if align_level is not None else _level_volts(s, al_ch, trig.level)
            if lvl is None:
                raise ValueError("align_level is required when the trigger source is not CH1/CH2")
            margin = 16
            ra = max(0, min(a, pre - margin) - margin)
            rb = min(trig.total_samples, max(a + cnt, pre + margin) + margin)
        else:
            lvl = None
            ra, rb = a, a + cnt
        read_chans = list(dict.fromkeys(chans + ([al_name] if align == "edge" else [])))
        avgs = {c: A.Averager() for c in chans}
        reasons: collections.Counter[str] = collections.Counter()
        shifts: list[float] = []
        noise_est: list[float] = []
        for i in range(n):
            cap = await anyio.to_thread.run_sync(
                lambda: s.capture(read_chans, timeout_s, ra, rb - ra, ctx=cctx))
            if cap is not None and shared and cap.settings_seq != cctx.settings_seq:
                raise _SettingsChanged(i, cap.settings_seq)
            if cap is None:
                reasons["no_trigger"] += 1
                if shared:
                    st = await anyio.to_thread.run_sync(s.status)
                    if st.settings_seq != cctx.settings_seq:
                        raise _SettingsChanged(i, st.settings_seq)
            elif cap.auto_timeout and trig.mode != 0:
                reasons["auto_timeout"] += 1
            else:
                off = a - ra
                clipped = any(cap.channels[c].clipped[off: off + cnt].any() for c in chans)
                if reject_clipped and clipped:
                    reasons["clipped"] += 1
                else:
                    shift = 0.0
                    ok = True
                    if align == "edge":
                        v_al = cap.channels[al_name].volts
                        x = A.find_edge_near(v_al, lvl, al_edge, pre - ra, 12)  # type: ignore[arg-type]
                        if x is None:
                            reasons["no_edge"] += 1
                            ok = False
                        else:
                            shift = x - (pre - ra)
                            shifts.append(shift)
                    if ok:
                        for c in chans:
                            v = cap.channels[c].volts
                            vv = A.shift_fractional(v, shift)[off: off + cnt] if shift else v[off: off + cnt]
                            cl = cap.channels[c].clipped[off: off + cnt]
                            avgs[c].add(vv, cl)
                        if len(noise_est) < 4:
                            ch0 = cap.channels[chans[0]]
                            d = np.diff(ch0.codes[off: off + cnt])
                            noise_est.append(float(np.median(np.abs(d - np.median(d))) * 1.4826 / math.sqrt(2)))
            if ctx is not None and (i % 8 == 7 or i == n - 1):
                await _progress(ctx, i + 1, n, f"{avgs[chans[0]].n} used, {sum(reasons.values())} dropped")
        used = avgs[chans[0]].n
        if used == 0:
            raise ToolError(f"{tool}: all {n} captures were dropped ({dict(reasons)}). Fix: "
                            + ("increase volts_per_div or set reject_clipped=false; " if reasons["clipped"] else "")
                            + ("check trigger level/source; " if reasons["no_trigger"] else "")
                            + ("check align_channel/align_level; " if reasons["no_edge"] else ""))
        tt = (a + np.arange(cnt) - pre) * dt
        return {"mode": "host", "t": tt, "mean": {c: avgs[c].mean for c in chans},
                "std": {c: avgs[c].std for c in chans}, "clipped_any": {c: avgs[c].clipped_any for c in chans},
                "n_used": used, "n_dropped": sum(reasons.values()), "drop_reasons": dict(reasons),
                "noise_single": float(np.median(noise_est)) if noise_est else None,
                "notes": notes, "dt": dt, "edge_shifts": np.array(shifts) if shifts else None}

    async def _hw_averaged_run(s: Scope, cctx: CaptureContext, *, n: int, chans: list[str], a: int, cnt: int,
                               align: str, timeout_s: float, reject_clipped: bool, tool: str,
                               notes: list[str]) -> dict[str, Any]:
        """Protocol 1.17 hardware averaging: one run of n records summed in the FPGA (bitstream 0x1536)."""
        if not s.profile().has(P.Caps.SEQ):
            raise ToolError(f"{tool}: hardware averaging needs FPGA bitstream 0x1536 and firmware 0.2.58 or later "
                            "(caps SEQ); use hardware=false")
        if align != "hardware":
            raise ToolError(f"{tool}: hardware averaging aligns on the hardware trigger only; use align='hardware'")
        if reject_clipped:
            notes.append("hardware averaging: clipped records cannot be rejected (the FPGA sums every triggered "
                         "record); check the clipped flag of the mean and the range")
        trig = cctx.trigger
        word_s = cctx.timebase.dt * (1 if cctx.decim is not None and cctx.decim.k else 2)
        est = n * (trig.total_words * word_s + P.seq_dead_ns(trig.total_words, trig.pretrigger_words,
                                                             int(word_s * 1e9)) * 1e-9)
        try:
            run = await anyio.to_thread.run_sync(
                lambda: s.capture_hw_average(chans, n, timeout_s=timeout_s + 2 * est + 1.0))
        except SeqError as e:
            raise ToolError(f"{tool}: {e}. Hardware averaging takes records up to 2046 words (4092 samples 8-bit, "
                            "2046 with the decimation filter) and at most 257 records with 8-bit samples; shorten "
                            "total_samples with scope_set_trigger, lower n, or use hardware=false") from None
        if s.shared and run.capture_info.header.settings_seq != cctx.settings_seq:
            raise _SettingsChanged(0, run.capture_info.header.settings_seq)
        cap = run.positions[0]
        tt = (a + np.arange(cnt) - trig.pretrigger_samples) * cctx.timebase.dt
        means = {c: cap.channels[c].volts[a: a + cnt] for c in chans}
        clipped = {c: cap.channels[c].clipped[a: a + cnt] for c in chans}
        notes = notes + list(run.notes) + [
            "hardware averaging (FPGA 0x1536, protocol 1.17): the mean of the records summed in the FPGA, read as "
            "exact sums; not yet verified on the unit against READ_RAW (bench check 9 of firmware/AGENTS.md "
            "section 35)"]
        if any(v.any() for v in clipped.values()):
            notes.append("the mean sits at a rail in some samples (clipped records): raise volts_per_div")
        used = run.records[0]
        return {"mode": "hardware", "t": tt, "mean": means, "std": None, "clipped_any": None, "n_used": used,
                "n_dropped": run.run.untriggered, "drop_reasons": {"untriggered": run.run.untriggered},
                "noise_single": None, "notes": notes, "dt": cctx.timebase.dt, "edge_shifts": None,
                "capture_id": run.capture_id}

    def _avg_summary(res: dict[str, Any], chans: list[str]) -> dict[str, Any]:
        out: dict[str, Any] = {}
        for c in chans:
            m = res["mean"][c]
            d: dict[str, Any] = {k: _r6(v) for k, v in A.stats(m).items()}
            if res["std"] is not None:
                sd = res["std"][c]
                d["std_per_sample_median"] = _r6(float(np.median(sd)))
                d["noise_after_estimate"] = _r6(float(np.median(sd)) / math.sqrt(max(res["n_used"], 1)))
                d["clipped_samples_any_capture"] = int(res["clipped_any"][c].sum())
            out[c] = d
        return out

    @register(1, title="Averaged capture")
    async def scope_averaged_capture(
        ctx: Context,
        n: Annotated[int, Field(ge=1, le=4096, description="number of captures to average")] = 64,
        channels: Annotated[list[str] | None, Field(description="channels to average, default ['CH1']")] = None,
        window: Annotated[list[float | str] | None, Field(description="[t0, t1] seconds relative to the trigger; "
                                                                      "only this part is read and averaged")] = None,
        align: Annotated[Literal["hardware", "edge"], Field(description="'hardware': trust the trigger point "
                         "(2-sample jitter); 'edge': per capture find the crossing of align_channel at align_level "
                         "with linear interpolation and shift by the sub-sample offset")] = "hardware",
        align_channel: Annotated[Literal["CH1", "CH2"] | None, Field(description="channel carrying the reference "
                                 "edge, typically CH2 with the detector's TX gate; default: trigger source")] = None,
        align_level: Annotated[float | str | None, Field(description="volts; default: the trigger level")] = None,
        timeout_s: Annotated[float, Field(gt=0, le=10, description="per capture; timeouts are dropped and "
                                                                   "counted")] = 1.0,
        reject_clipped: Annotated[bool, Field(description="drop captures with clipped samples in the "
                                                          "window")] = True,
        on_device: Annotated[bool | None, Field(description="use the firmware's ACQUIRE_AVG when available and "
                                                            "align='hardware' (default: auto)")] = None,
        hardware: Annotated[bool, Field(description="average in the FPGA (bitstream 0x1536, protocol 1.17): one "
                            "run sums n records without reading each one, so it is much faster; align='hardware' "
                            "only, records up to 2046 words, n <= 257 with 8-bit samples (65536 with the decimation "
                            "filter); no per-sample std. Not yet verified on the unit")] = False,
    ) -> dict[str, Any]:
        """Average n triggered captures: the main tool for detector work. Returns per channel statistics of the
        mean waveform, the per-sample standard deviation, n_used, n_dropped with reasons, noise per sample
        before/after, an effective-resolution estimate, captures per second, and a .npz file with t, mean and
        std per channel.

        Averaging reduces random noise by sqrt(n) but not the A/B interleave mismatch, the 8-bit quantisation of
        a noise-free signal, or trigger jitter (up to 2 / fs with hardware alignment). Edge alignment needs a
        clean edge on align_channel with at least 3 samples on the slope.

        The scope is shared with the person at it: if a setting (range, coupling, timebase, trigger, ...) is
        changed at the scope during the run, the run restarts with the new settings and 'restarts' lists when
        and what changed (gives up after 5 restarts). The scope screen shows the host captures."""
        args = {"n": n, "channels": channels, "window": window, "align": align, "align_channel": align_channel,
                "align_level": align_level, "timeout_s": timeout_s, "reject_clipped": reject_clipped,
                "on_device": on_device, "hardware": hardware}
        try:
            chans = _channels_arg(channels, ["CH1"])
            w = _window_arg(window)
            res = await _averaged(ctx, n=n, chans=chans, w=w, align=align, align_channel=align_channel,
                                  align_level=_q(align_level, "V"), align_edge=None, timeout_s=timeout_s,
                                  reject_clipped=reject_clipped, on_device=on_device, tool="scope_averaged_capture",
                                  hardware=hardware)
        except (DeviceError, TransportError, ValueError) as e:
            sess.audit("scope_averaged_capture", 1, args, f"error: {e}")
            raise ToolError(_err_text("scope_averaged_capture", e)) from e
        arrays = {"t": res["t"]}
        for c in chans:
            arrays[f"{c}_mean"] = res["mean"][c]
            if res["std"] is not None:
                arrays[f"{c}_std"] = res["std"][c]
                arrays[f"{c}_clipped_any"] = res["clipped_any"][c]
        if res["edge_shifts"] is not None:
            arrays["edge_shifts_samples"] = res["edge_shifts"]
        meta = {"kind": "averaged_capture", "args": args, "restarts": res["restarts"], "n_used": res["n_used"],
                "n_dropped": res["n_dropped"],
                "drop_reasons": res["drop_reasons"], "dt": res["dt"], "mode": res["mode"], "scaling": SCALING_CAVEAT}
        path = sess.save_npz("avg", arrays, meta)
        ns = res["noise_single"]
        out: dict[str, Any] = {
            "ok": True, "n_used": res["n_used"], "n_dropped": res["n_dropped"], "drop_reasons": res["drop_reasons"],
            "averaging": res["mode"], "align": align, "dt": _r6(res["dt"]), "n_samples": int(res["t"].size),
            "t_start": _r6(float(res["t"][0])), "t_end": _r6(float(res["t"][-1])),
            "channels": _avg_summary(res, chans),
            "noise_single_capture_lsb": _r6(ns),
            "noise_after_lsb_estimate": _r6(ns / math.sqrt(res["n_used"])) if ns is not None else None,
            "effective_bits_estimate": _r6(A.effective_bits(ns if ns is not None else 1.0, res["n_used"])),
            "captures_per_second": _r6((res["n_used"] + res["n_dropped"]) / max(res["elapsed"], 1e-9)),
            "restarts": res["restarts"], "file": path,
        }
        if res["edge_shifts"] is not None:
            es = res["edge_shifts"]
            out["edge_shift_samples"] = {"mean": _r6(float(es.mean())), "std": _r6(float(es.std())),
                                         "min": _r6(float(es.min())), "max": _r6(float(es.max()))}
        if res["t"].size <= sess.cfg.inline_max_points:
            out["mean"] = {"t": A._r(res["t"]), **{c: A._r(res["mean"][c]) for c in chans}}
        else:
            out["preview"] = {c: A.preview(res["t"], res["mean"][c]) for c in chans}
        cav = list(res["notes"])
        cav.append("Averaging reduces random noise by sqrt(n) but not the A/B interleave mismatch, the 8-bit "
                   "quantisation of a noise-free signal, or trigger jitter (up to 2 / fs with hardware alignment).")
        if res["mode"] == "hardware":
            out["capture_id"] = res["capture_id"]
        if align == "edge":
            cav.append("edge alignment needs a clean edge on align_channel with at least 3 samples on the slope")
        if res["n_dropped"]:
            cav.append(f"{res['n_dropped']} captures dropped: {res['drop_reasons']}")
        if ns is not None and ns < 0.5:
            cav.append(f"single-capture noise is only {ns:.2f} LSB rms: too little dither, averaging cannot "
                       "resolve below one code")
        out["caveats"] = cav + [SCALING_CAVEAT]
        sess.audit("scope_averaged_capture", 1, args, "ok", {"n_used": res["n_used"], "file": path})
        return out

    @register(1, title="Segmented capture")
    async def scope_capture_segments(
        count: Annotated[int, Field(ge=1, le=256, description="segments (records) to capture; each is one "
                                    "trigger. At most 8192 / slot words (the slot is the record rounded up to a "
                                    "power of two)")] = 16,
        channels: Annotated[list[str] | None, Field(description="channels to read, default ['CH1']")] = None,
        holdoff: Annotated[float | str, Field(description="seconds to wait after each segment before re-arming, "
                           "e.g. '10us' (rounded up to 5 ns)")] = 0.0,
        circular: Annotated[bool, Field(description="keep refilling the slots until timeout_s, then stop: the "
                            "table holds the latest segments (count is ignored: every slot is used)")] = False,
        timeout_s: Annotated[float, Field(gt=0, le=60, description="the run is stopped after this long; it keeps "
                             "the segments it has (circular: the run time)")] = 2.0,
    ) -> dict[str, Any]:
        """Segmented capture (FPGA bitstream 0x1536, firmware 0.2.58+, protocol 1.17): the FPGA re-arms itself
        after every trigger and stores `count` records with the live trigger and record length
        (scope_set_trigger total_samples / pretrigger), each with a 5 ns trigger timestamp, without the host
        reading in between (re-arm in about hold-off + pretrigger + a few clocks). Use it for bursts, pulse
        trains and per-pulse detector captures: timing between events plus every waveform.

        Returns per segment the trigger time relative to the first one, the interval to the previous one, whether
        it triggered (auto mode records untriggered segments) and per channel min / max / mean / Vpp; interval
        statistics; and a .npz with t (record time axis), <CH>_volts as (segments, samples), timestamps_s,
        triggered and slots. Records must fit a slot of at most 4096 words minus 2 (8190 samples 8-bit, 4094 with
        the decimation filter). Not yet verified on the unit."""
        args = {"count": count, "channels": channels, "holdoff": holdoff, "circular": circular,
                "timeout_s": timeout_s}

        def go() -> dict[str, Any]:
            notes = sess.ensure_control()
            s = sess.get_scope()
            if not s.profile().has(P.Caps.SEQ):
                raise ToolError("scope_capture_segments needs FPGA bitstream 0x1536 and firmware 0.2.58 or later "
                                "(caps SEQ)")
            chans = _channels_arg(channels, ["CH1"])
            hold = _q(holdoff, "s") or 0.0
            if not 0 <= hold <= 20.0:
                raise ToolError("holdoff must be 0..20 s")
            try:
                run = s.capture_segments(chans, count, holdoff_ns=int(math.ceil(hold * 1e9)), circular=circular,
                                         timeout_s=timeout_s)
            except SeqError as e:
                raise ToolError(f"scope_capture_segments: {e}. Shorten total_samples with scope_set_trigger or "
                                "lower count") from None
            segs = [g for g in run.segments if g.capture is not None]
            if not segs:
                return {"ok": False, "segments": 0, "caveats": notes + run.notes + [
                    f"no segment within {timeout_s} s: check the trigger with scope_get_config, use mode='auto', "
                    "or raise timeout_s"]}
            segs.sort(key=lambda g: g.entry.timestamp)
            t0 = segs[0].t_trigger
            ts = np.array([g.t_trigger - t0 for g in segs])
            first = segs[0].capture
            assert first is not None
            arrays: dict[str, np.ndarray] = {"t": first.t, "timestamps_s": ts,
                                             "triggered": np.array([g.entry.triggered for g in segs]),
                                             "slots": np.array([g.slot for g in segs])}
            rows = []
            for g, tt in zip(segs, ts):
                cap = g.capture
                assert cap is not None
                row: dict[str, Any] = {"slot": g.slot, "t": _r6(float(tt)), "triggered": g.entry.triggered}
                for c in chans:
                    st = A.stats(cap.channels[c].volts)
                    row[c] = {k: _r6(st[k]) for k in ("min", "max", "mean", "vpp") if k in st}
                rows.append(row)
            for c in chans:
                arrays[f"{c}_volts"] = np.stack([g.capture.channels[c].volts for g in segs]).astype(np.float32)
                arrays[f"{c}_clipped"] = np.stack([g.capture.channels[c].clipped for g in segs])
            r = run.run
            meta = {"kind": "segments", "args": args, "capture_id": run.capture_id, "dt": first.dt,
                    "pretrigger_samples": first.pretrigger_samples, "total_samples": first.total_samples,
                    "run": r.to_dict(), "channels": {c: first.channels[c].config.to_dict() for c in chans},
                    "scaling": SCALING_CAVEAT}
            path = sess.save_npz("segments", arrays, meta)
            dts = np.diff(ts)
            out: dict[str, Any] = {
                "ok": True, "capture_id": run.capture_id, "segments": len(segs), "requested": r.requested,
                "completed": r.count, "untriggered": r.untriggered, "circular": bool(r.flags & P.SEQ_RUN_CIRCULAR),
                "stopped": bool(r.flags & P.SEQ_RUN_STOPPED), "wrapped": bool(r.flags & P.SEQ_RUN_WRAPPED),
                "slot_words": 1 << r.m, "dt": _r6(first.dt), "samples_per_segment": first.n,
                "interval_s": {"mean": _r6(float(dts.mean())), "min": _r6(float(dts.min())),
                               "max": _r6(float(dts.max()))} if dts.size else None,
                "file": path}
            out["table" if len(rows) <= 64 else "table_first_64"] = rows[:64]
            cav = list(run.notes)
            if r.flags & P.SEQ_RUN_STOPPED and not r.flags & P.SEQ_RUN_CIRCULAR:
                cav.append(f"stopped after {timeout_s} s with {r.count} of {r.requested} segments")
            if r.untriggered:
                cav.append(f"{r.untriggered} segment(s) ended on the auto time-out: not synchronised to the signal")
            cav.append("timestamps: 5 ns resolution from the run start (one trigger-latch uncertainty of a write "
                       "clock); segmented capture is not yet verified on the unit")
            out["caveats"] = notes + cav + STANDING_CAVEATS
            return out
        return await run("scope_capture_segments", 1, args, go)

    @register(1, title="Capture and FFT")
    async def scope_capture_and_fft(
        ctx: Context,
        channel: Annotated[Literal["CH1", "CH2"], Field(description="channel to analyse")] = "CH1",
        n_avg: Annotated[int, Field(ge=1, le=1024, description="power-spectrum averaging over captures")] = 1,
        window_fn: Annotated[Literal["hann", "flattop", "blackman-harris", "rect"], Field(
            description="flattop for amplitude accuracy")] = "hann",
        fmin: Annotated[float | str, Field(description="lowest reported frequency, Hz")] = 0.0,
        fmax: Annotated[float | str | None, Field(description="highest reported frequency, Hz (default fs/2)")] = None,
        peaks: Annotated[int, Field(ge=1, le=100, description="number of peaks to list")] = 10,
        detrend: Annotated[bool, Field(description="remove the mean before the FFT")] = True,
        timeout_s: Annotated[float, Field(gt=0, le=10, description="per capture")] = 2.0,
    ) -> dict[str, Any]:
        """Capture (optionally n_avg times), compute the amplitude spectrum and list peaks (frequency, dBV rms,
        dBc), the noise floor and the THD of the strongest peak. The spectrum is saved as .npz.

        8-bit ADC: SFDR around 45-50 dBc at best; spurs below that are not trustworthy. Expect a spur at fs/2 and
        images at fs/2 - f from core A/B mismatch. Content above ~30 MHz is attenuated by the front end."""
        args = {"channel": channel, "n_avg": n_avg, "window_fn": window_fn, "fmin": fmin, "fmax": fmax,
                "peaks": peaks, "detrend": detrend}

        def one(cctx: CaptureContext) -> tuple[np.ndarray, float, int, bool, int, bool]:
            s = sess.get_scope()
            cap = s.capture([channel], timeout_s, ctx=cctx)
            if cap is None:
                raise ToolError(f"scope_capture_and_fft: no trigger within {timeout_s} s; use mode='auto' or check "
                                "the trigger")
            ch = cap.channels[channel]
            changed = s.shared and cap.settings_seq != cctx.settings_seq
            return ch.volts, cap.dt, cap.capture_id, cap.auto_timeout, int(ch.clipped.sum()), changed

        held = _Held(sess)
        try:
            try:
                await anyio.to_thread.run_sync(held.acquire)
                notes = await anyio.to_thread.run_sync(sess.ensure_control)
                psum = None
                f = None
                enbw = 0.0
                dt = 0.0
                clipped = 0
                cid = 0
                restarts = 0
                cctx = await anyio.to_thread.run_sync(sess.get_scope().capture_context)
                i = 0
                while i < n_avg:
                    v, dt, cid, _, nclip, changed = await anyio.to_thread.run_sync(one, cctx)
                    if changed:
                        restarts += 1
                        if restarts > cfg.max_restarts:
                            raise ToolError("scope_capture_and_fft: settings kept changing at the scope; retry when "
                                            "they are left alone")
                        cctx = await anyio.to_thread.run_sync(sess.get_scope().capture_context)
                        psum, clipped, i = None, 0, 0
                        continue
                    i += 1
                    clipped += nclip
                    f, amp, enbw = A.amplitude_spectrum(v, dt, window_fn, detrend)
                    psum = amp ** 2 if psum is None else psum + amp ** 2
                    if n_avg > 1 and (i % 8 == 0 or i == n_avg):
                        await _progress(ctx, i, n_avg)
            finally:
                held.release()
        except (DeviceError, TransportError, ValueError) as e:
            raise ToolError(_err_text("scope_capture_and_fft", e)) from e
        assert psum is not None and f is not None
        amp = np.sqrt(psum / n_avg)
        fs = 1 / dt
        f_lo = parse_quantity(fmin)
        f_hi = parse_quantity(fmax) if fmax is not None else fs / 2
        band = (f >= f_lo) & (f <= f_hi)
        floor = float(np.median(amp[band][1:])) if band.sum() > 2 else float(np.median(amp))
        pk = A.peaks(f, amp, peaks, f_lo, f_hi, floor=floor * 3)
        ref = pk[0][1] if pk else 1.0
        plist = [{"frequency": _r6(fr), "dbv": _r6(A.db(a)), "dbc": _r6(A.db(a, ref)), "vrms": _r6(a)} for fr, a, _ in pk]
        thd = A.thd(f, amp, pk[0][2]) if pk else None
        path = sess.save_npz("fft", {"f": f, "amplitude_vrms": amp},
                             {"kind": "fft", "args": args, "dt": dt, "enbw_hz": enbw, "capture_id": cid})
        if restarts:
            notes = list(notes) + [f"settings were changed at the scope during the run: restarted {restarts} time(s); "
                                   "the spectrum uses only captures with the final settings"]
        cav = list(notes) + [
            "8-bit ADC: SFDR around 45-50 dBc at best; spurs below that are not trustworthy.",
            "expect a spur at fs/2 and images at fs/2 - f from core A/B mismatch",
            "content above ~30 MHz is attenuated by the front end",
            f"spectral leakage: window={window_fn} has resolution bandwidth {fmt_si(enbw, 'Hz')}"]
        if clipped:
            cav.append(f"{clipped} samples clipped: harmonics are distorted; increase volts_per_div")
        out = {"ok": True, "sample_rate": _r6(fs), "frequency_resolution": _r6(f[1] - f[0]), "enbw_hz": _r6(enbw),
               "n_avg": n_avg, "restarts": restarts, "peaks": plist, "noise_floor_dbv_per_bin": _r6(A.db(floor)),
               "thd_percent": _r6(thd * 100) if thd is not None else None, "file": path, "caveats": cav}
        sess.audit("scope_capture_and_fft", 1, args, "ok", {"file": path})
        return out

    @register(1, title="Fit a pulse-induction decay")
    async def scope_decay_fit(
        ctx: Context,
        channel: Annotated[Literal["CH1", "CH2"], Field(description="the decay signal")] = "CH1",
        edge_channel: Annotated[Literal["CH1", "CH2", "none"] | None, Field(
            description="channel with the TX turn-off edge; 'none' uses the hardware trigger point; default: the "
                        "trigger source")] = None,
        edge: Annotated[Literal["falling", "rising"], Field(description="TX turn-off edge direction")] = "falling",
        edge_level: Annotated[float | str | None, Field(description="volts on edge_channel; default: trigger "
                                                                    "level")] = None,
        t_start: Annotated[float | str, Field(description="start of the fit after the edge, skips flyback and "
                                                          "recovery")] = 10e-6,
        t_end: Annotated[float | str, Field(description="end of the fit after the edge")] = 200e-6,
        model: Annotated[Literal["exp", "exp+offset", "exp2", "power"], Field(
            description="exp2 = two exponentials (target + ground/coil); power = A t^-k + c for ferrous/viscous "
                        "ground")] = "exp+offset",
        n_avg: Annotated[int, Field(ge=1, le=4096, description="captures averaged first (edge-aligned when "
                                                               "edge_channel is set)")] = 64,
        baseline: Annotated[list[float | str] | None, Field(description="[t0, t1] window (relative to the edge) "
                            "to estimate the offset instead of fitting it")] = None,
        blank_after_clip: Annotated[float | str, Field(description="exclude samples this long after any clipped "
                                                                   "sample")] = 2e-6,
        sample_points: Annotated[list[float | str] | None, Field(description="delays after the edge at which to "
                                 "report the averaged signal, the way PI detectors sample the decay")] = None,
        sample_width: Annotated[float | str, Field(description="integration width for sample_points")] = 2e-6,
        timeout_s: Annotated[float, Field(gt=0, le=10, description="per capture")] = 1.0,
    ) -> dict[str, Any]:
        """Fit the receive decay after a TX turn-off edge (pulse induction): averages n_avg captures (edge-aligned
        on edge_channel), then fits the model between t_start and t_end after the edge. Returns tau (and tau2),
        amplitude(s), offset, the window actually used, R^2, residual rms, number of samples, sample-point
        values, and a .npz file with t, data, fit and residuals.

        The flyback spike of a PI coil can be hundreds of volts: use a 10x probe or a clamp and check the input
        rating before connecting. After clipping, the front end needs recovery time; samples within
        blank_after_clip are excluded. With fs = F the decay is sampled every 1/F; tau below ~5 samples is not
        resolvable. AC coupling distorts decays longer than the coupling time constant; use DC coupling."""
        args = {"channel": channel, "edge_channel": edge_channel, "edge": edge, "edge_level": edge_level,
                "t_start": t_start, "t_end": t_end, "model": model, "n_avg": n_avg, "baseline": baseline,
                "blank_after_clip": blank_after_clip, "sample_points": sample_points, "sample_width": sample_width}
        try:
            ts, te = parse_quantity(t_start, "s"), parse_quantity(t_end, "s")
            if te <= ts:
                raise ValueError(f"t_end ({te:g} s) must be after t_start ({ts:g} s)")
            bl = _window_arg(baseline)
            lo = min(ts, bl[0]) if bl else ts
            hi = max(te, bl[1]) if bl else te
            use_edge = edge_channel != "none"
            res = await _averaged(ctx, n=n_avg, chans=[channel], w=(lo, hi),
                                  align="edge" if use_edge else "hardware",
                                  align_channel=None if edge_channel in (None, "none") else edge_channel,
                                  align_level=_q(edge_level, "V"), align_edge=edge, timeout_s=timeout_s,
                                  reject_clipped=False, on_device=False if use_edge else None,
                                  tool="scope_decay_fit")
            t = res["t"]
            y = res["mean"][channel]
            dt = res["dt"]
            excl = np.zeros(t.size, dtype=bool)
            if res["clipped_any"] is not None:
                nb = int(math.ceil(parse_quantity(blank_after_clip, "s") / dt))
                excl = A.blank_after_clip(res["clipped_any"][channel], nb)
            fit_sel = (t >= ts) & (t <= te) & ~excl
            off = None
            if bl:
                bsel = (t >= bl[0]) & (t <= bl[1]) & ~excl
                if bsel.sum() < 3:
                    raise ValueError("baseline window has fewer than 3 usable samples")
                off = float(np.mean(y[bsel]))
            if fit_sel.sum() < 5:
                raise ValueError(f"only {int(fit_sel.sum())} usable samples between t_start and t_end (after "
                                 "blanking clipped samples); widen the window, lower blank_after_clip or raise the "
                                 "sample rate")
            fr = A.fit_decay(t[fit_sel], y[fit_sel], model, offset=off)
        except (DeviceError, TransportError, ValueError) as e:
            sess.audit("scope_decay_fit", 1, args, f"error: {e}")
            raise ToolError(_err_text("scope_decay_fit", e)) from e
        resid = np.full(t.size, np.nan)
        fitfull = np.full(t.size, np.nan)
        fitfull[fit_sel] = fr.fit
        resid[fit_sel] = y[fit_sel] - fr.fit
        arrays = {"t": t, "data": y, "fit": fitfull, "residual": resid, "fit_mask": fit_sel, "excluded": excl}
        if res["std"] is not None:
            arrays["std"] = res["std"][channel]
        path = sess.save_npz("decay", arrays, {"kind": "decay_fit", "args": args, "fit": fr.to_dict(),
                                               "n_used": res["n_used"], "dt": dt})
        tsel = t[fit_sel]
        out: dict[str, Any] = {"ok": fr.ok, "fit": fr.to_dict(),
                               "tau": _r6(fr.params.get("tau")), "tau2": _r6(fr.params.get("tau2")),
                               "amplitude": _r6(fr.params.get("amplitude")), "offset": _r6(fr.params.get("offset")),
                               "r2": _r6(fr.r2), "residual_rms": _r6(fr.residual_rms), "n_samples": fr.n,
                               "fit_window": [_r6(float(tsel[0])), _r6(float(tsel[-1]))],
                               "excluded_samples": int(excl[(t >= ts) & (t <= te)].sum()),
                               "n_used": res["n_used"], "n_dropped": res["n_dropped"],
                               "drop_reasons": res["drop_reasons"], "alignment": "edge" if use_edge else "hardware",
                               "restarts": res["restarts"], "file": path}
        if sample_points:
            dl = [parse_quantity(d, "s") for d in sample_points]
            out["sample_points"] = A.sample_points(t, y, dl, parse_quantity(sample_width, "s"))
        cav = list(res["notes"]) + [
            "The flyback spike of a PI coil can be hundreds of volts: use a 10x probe or a clamp and check the "
            "input rating before connecting.",
            "after clipping, the front end needs recovery time; samples within blank_after_clip are excluded"]
        tau = fr.params.get("tau")
        if tau is not None and tau < 5 * dt:
            cav.append(f"tau {tau:.3g} s is below 5 samples ({5 * dt:.3g} s): not resolvable, raise the sample rate")
        if not fr.ok:
            cav.append(fr.message)
        try:
            s = sess.scope
            if s is not None and s.get_channel(channel_index(channel)).coupling == 1:
                cav.append("AC coupling distorts decays longer than the coupling time constant; use DC coupling")
        except Exception:  # noqa: BLE001
            pass
        out["caveats"] = cav + [SCALING_CAVEAT]
        sess.audit("scope_decay_fit", 1, args, "ok", {"tau": out["tau"], "file": path})
        return out

    @register(0, title="Measure")
    async def scope_measure(
        items: Annotated[list[Literal["vpp", "vmin", "vmax", "mean", "rms", "ac_rms", "frequency", "period", "duty",
                                      "rise_time", "fall_time", "delay"]] | None,
                         Field(description="measurements; 'delay' = CH1 -> CH2 at the first rising mid-level "
                                           "crossing. Default: all")] = None,
        channels: Annotated[list[str] | None, Field(description="default: channels of the last capture")] = None,
        capture: Annotated[bool, Field(description="take a new capture first (tier 1); otherwise use the last "
                                                   "capture of this server, or the UI trace in LOCAL")] = False,
        timeout_s: Annotated[float, Field(gt=0, le=10)] = 2.0,
    ) -> dict[str, Any]:
        """Measurements computed on the host from raw data with calibration: vpp, vmin, vmax, mean, rms, ac_rms,
        frequency, period, duty, rise_time, fall_time, delay(CH1->CH2). Tier 0 on an existing capture, tier 1 if
        capture=true.

        Rise times below ~12 ns (0.35 / 30 MHz) measure the scope, not the signal."""
        all_items = ["vpp", "vmin", "vmax", "mean", "rms", "ac_rms", "frequency", "period", "duty", "rise_time",
                     "fall_time", "delay"]
        sel = items or all_items
        args = {"items": sel, "channels": channels, "capture": capture}
        if capture:
            need_tier(1, "scope_measure(capture=true)")

        def go() -> dict[str, Any]:
            s = sess.get_scope()
            notes: list[str] = []
            chans = _channels_arg(channels, ["CH1", "CH2"] if "delay" in sel else ["CH1"])
            if capture:
                notes += sess.ensure_control()
                cap = s.capture(chans, timeout_s)
                if cap is None:
                    raise ToolError(f"scope_measure: no trigger within {timeout_s} s")
                sess.last_capture = cap
            else:
                cap = sess.last_capture
                if cap is None or any(c not in cap.channels for c in chans):
                    st = s.status()
                    if st.shared and st.capture_id:
                        cap = s.read_capture(chans)
                    elif st.control != P.Control.REMOTE and not st.shared:
                        cap = s.read_trace_capture(chans)
                    elif st.acq_state == P.AcqState.DONE:
                        cap = s.read_capture(chans, st=st)
                    else:
                        raise ToolError("scope_measure: no capture available; call scope_capture or pass "
                                        "capture=true")
            res: dict[str, Any] = {}
            for c in chans:
                v = cap.channels[c].volts
                st_ = A.stats(v)
                fr = A.frequency(v, cap.dt)
                m: dict[str, Any] = {}
                for it in sel:
                    if it == "vmin":
                        m[it] = _r6(st_["min"])
                    elif it == "vmax":
                        m[it] = _r6(st_["max"])
                    elif it in ("vpp", "mean", "rms", "ac_rms"):
                        m[it] = _r6(st_[it])
                    elif it in ("frequency", "period", "duty"):
                        m[it] = _r6(fr[it]) if fr[it] is not None else None
                    elif it == "rise_time":
                        m[it] = _r6(A.transition_time(v, cap.dt, "rising"))
                    elif it == "fall_time":
                        m[it] = _r6(A.transition_time(v, cap.dt, "falling"))
                res[c] = m
            out: dict[str, Any] = {"ok": True, "capture_id": cap.capture_id, "dt": _r6(cap.dt), "channels": res}
            if "delay" in sel:
                if "CH1" in cap.channels and "CH2" in cap.channels:
                    out["delay_ch1_to_ch2"] = _r6(A.delay(cap.channels["CH1"].volts, cap.channels["CH2"].volts, cap.dt))
                else:
                    notes.append("delay needs both CH1 and CH2 in the capture")
            for c in chans:
                rt = res[c].get("rise_time")
                if rt is not None and rt < 12e-9:
                    notes.append(f"{c}: rise time below ~12 ns (0.35 / 30 MHz) measures the scope, not the signal")
            out["caveats"] = notes + [SCALING_CAVEAT]
            return out
        return await run("scope_measure", 1 if capture else 0, args, go)

    @register(2, title="Calibrate volts with a known DC voltage")
    async def scope_calibrate_volts(
        channel: Annotated[Literal["CH1", "CH2"], Field(description="channel the known voltage is connected to")],
        applied_volts: Annotated[float | str, Field(description="the DC voltage at the probe tip, read with a DMM "
                                                                "(0 V = input shorted), e.g. 0.3 or '1V'")],
        ranges: Annotated[list[int] | None, Field(description="range indexes 0..6 to measure (default all); ranges "
                                                              "where the voltage clips are skipped")] = None,
        captures: Annotated[int, Field(ge=1, le=64, description="captures averaged per range")] = 4,
        reset: Annotated[bool, Field(description="drop this channel's stored points first")] = False,
    ) -> dict[str, Any]:
        """Measure a known DC voltage on every range and fit the per-unit voltage calibration (bench test T8):
        per range and ADC core the raw code of 0 V, volts per code and the offset-PWM slope, from all points stored
        so far. Points accumulate across calls: use at least two voltages (e.g. 0 V and about 3 divisions of the
        most sensitive range in use) and, to model moving the trace, the same voltage at two trace offsets. The fit
        is saved to the calibration file and used for every conversion from then on (results say scaling=
        'unit-calibration').

        Switches the channel's range relays; the range and the trigger mode are restored afterwards. The channel
        must be DC coupled and the voltage steady DC: ranges with a ripple above 4 codes are skipped."""
        args = {"channel": channel, "applied_volts": applied_volts, "ranges": ranges, "captures": captures,
                "reset": reset}

        def go() -> dict[str, Any]:
            notes = sess.ensure_control()
            s = sess.get_scope()
            ch = channel_index(channel)
            v_tip = parse_quantity(applied_volts, "V")
            rr = list(range(7)) if ranges is None else sorted(set(int(r) for r in ranges))
            if any(not 0 <= r <= 6 for r in rr):
                raise ValueError("ranges must be 0..6")
            saved_ch = s.get_channel(ch, cached=False)
            if saved_ch.coupling != 0:
                raise ValueError(f"{channel} is AC coupled; calibrate with DC coupling (scope_set_channel "
                                 f"coupling='DC')")
            saved_tr = s.get_trigger(cached=False)
            path = sess.cal_path()
            cal = s.unit_cal or UnitCalibration.load_or_new(path, s.info.serial_str)
            if reset:
                cal.points = [p for p in cal.points if p["channel"] != ch]
            v_bnc = v_tip / saved_ch.probe_factor
            rows, skipped = [], []
            try:
                if saved_tr.mode != 0:
                    s.set_trigger(mode=0)
                for r in rr:
                    if min(r, 5) != min(s.get_channel(ch).range, 5):
                        sess.relay_slot()
                    s.set_channel(ch, range_index=r)
                    cfg = s.get_channel(ch, cached=False)
                    calr = s.get_cal(ch)
                    pre = s.scale(ch, cfg)  # the conversion before this point (to report the error it had)
                    a_m, b_m, a_s, b_s = [], [], [], []
                    for _ in range(captures):
                        cap = s.capture([channel], 2.0, keep_raw=True)
                        if cap is None:
                            raise ToolError("scope_calibrate_volts: no capture completed in auto mode")
                        cd = cap.channels[channel]
                        a_m.append(float(cd.raw_a.mean()))  # type: ignore[union-attr]
                        b_m.append(float(cd.raw_b.mean()))  # type: ignore[union-attr]
                        a_s.append(float(cd.raw_a.std()))  # type: ignore[union-attr]
                        b_s.append(float(cd.raw_b.std()))  # type: ignore[union-attr]
                    ma, mb = float(np.mean(a_m)), float(np.mean(b_m))
                    ripple = max(np.mean(a_s), np.mean(b_s))
                    if not (3 <= ma <= 252 and 3 <= mb <= 252):
                        skipped.append({"range": r, "reason": "clipped", "meanA": _r6(ma), "meanB": _r6(mb)})
                        continue
                    if ripple > 4:
                        skipped.append({"range": r, "reason": f"not steady DC (std {ripple:.1f} codes)"})
                        continue
                    cal.add_point(channel=ch, range_index=r, offset_pwm=cfg.offset_pwm, volts=v_bnc, meanA=ma,
                                  meanB=mb, fw_volts_per_lsb=cfg.volts_per_lsb, dc_shift_value=calr.dc_shift_value,
                                  source=f"scope_calibrate_volts {_dt.datetime.now().isoformat(timespec='seconds')}",
                                  center_cal=calr.dc_calibration_offset[r])
                    row: dict[str, Any] = {"range": r, "offset_pwm": cfg.offset_pwm, "meanA": _r6(ma),
                                           "meanB": _r6(mb), "std": _r6(float(ripple))}
                    prev = (pre.volts_raw(np.array([ma]), "A")[0] + pre.volts_raw(np.array([mb]), "B")[0]) / 2
                    row["volts_before"] = _r6(float(prev))
                    row["scaling_before"] = pre.source
                    rows.append(row)
            finally:
                s.set_channel(ch, range_index=saved_ch.range)
                if saved_tr.mode != 0:
                    s.set_trigger(mode=saved_tr.mode)
            cal.unit = cal.unit or s.info.serial_str
            cal.refit()
            saved = cal.save(path)
            s.unit_cal = cal
            for row in rows:
                f_a = cal.core_fit(ch, row["range"], "A")
                f_b = cal.core_fit(ch, row["range"], "B")
                if f_a and f_b:
                    row["fit"] = {"A": f_a.to_dict(), "B": f_b.to_dict()}
            cav = list(notes) + [f"calibration saved to {saved}; {len(cal.points)} points in the file"]
            if skipped:
                cav.append(f"{len(skipped)} range(s) skipped: {skipped}")
            n_volts = len({round(p['volts'], 4) for p in cal.points if p['channel'] == ch})
            if n_volts < 2:
                cav.append("only one voltage stored for this channel: zeros are fitted, gains stay the firmware's; "
                           "add a second voltage")
            return {"ok": True, "channel": channel, "applied_volts": v_tip, "measured": rows, "skipped": skipped,
                    "calibration_file": str(saved), "fit": describe_cal(cal),
                    "applied": {"points": len(cal.points)}, "caveats": cav}
        return await run("scope_calibrate_volts", 2, args, go)

    # ---- tier 3: output ----------------------------------------------------------------------------------

    @register(3, title="Square-wave generator", destructive=True, open_world=True)
    async def scope_generator(
        enabled: Annotated[bool, Field(description="false tri-states the calibration pin (no token needed)")],
        frequency: Annotated[float | str | None, Field(description="Hz, <= FNIRSI_MCP_GEN_MAX_HZ and <= 25 MHz, "
                                                                   "e.g. '10kHz'")] = None,
        duty_percent: Annotated[float | None, Field(ge=0, le=100, description="<= FNIRSI_MCP_GEN_MAX_DUTY")] = None,
        confirm_token: Annotated[str | None, Field(description="token from the confirmation response")] = None,
    ) -> dict[str, Any]:
        """Drive the calibration pin with a 3.3 V logic square wave (DDS, 11.64 mHz steps). The first call without
        confirm_token returns a token and a summary of the physical effect; repeat the identical call with the
        token within 120 s. Disabling needs no token. Returns the actual frequency, duty and enable state.

        The calibration output is a frequency/duty/timing reference, not a voltage reference: a ~3.1 Vpp square wave
        referenced to the USB ground; the probe ground is VRF1, +1.24 V above it, so it reads about -1.24 / +1.9 V.
        Square wave only, 3.3 V logic, about 20 ns edge jitter. Not a sine source and not a power driver: buffer
        it before driving a MOSFET gate. If the pin is wired into the detector (e.g. as a TX gate drive) this
        switches real coil current. On the stock FPGA the pin always outputs a fixed 1 kHz and this tool is
        unavailable."""
        args = {"enabled": enabled, "frequency": frequency, "duty_percent": duty_percent}

        def go() -> dict[str, Any]:
            s = sess.get_scope()
            if not s.profile().has_generator:
                raise ToolError("scope_generator: this FPGA bitstream has no programmable generator (stock FPGA: "
                                "fixed 1 kHz on the calibration pin)")
            f = parse_quantity(frequency, "Hz") if frequency is not None else None
            if f is not None:
                if not 0 < f <= min(cfg.gen_max_hz, 25e6):
                    raise ValueError(f"frequency {f:g} Hz not in (0, {min(cfg.gen_max_hz, 25e6):g}] "
                                     "(FNIRSI_MCP_GEN_MAX_HZ)")
            if duty_percent is not None and duty_percent > cfg.gen_max_duty:
                raise ValueError(f"duty {duty_percent}% above the configured ceiling {cfg.gen_max_duty}% "
                                 "(FNIRSI_MCP_GEN_MAX_DUTY, protects a TX stage driven from the pin)")
            if enabled and P.Caps.EXT_TRIGGER & s.profile().info.caps and s.get_trigger(cached=False).source == 2:
                raise ToolError("scope_generator: the calibration pin is the EXT trigger input now; set the trigger "
                                "source to CH1 or CH2 first (EXT and the generator share the pin)")
            if enabled:
                cur = s.get_gen()
                fs = f if f is not None else cur.frequency_hz
                du = duty_percent if duty_percent is not None else cur.duty * 100
                if du > cfg.gen_max_duty:
                    raise ValueError(f"current duty {du:.1f}% is above the ceiling {cfg.gen_max_duty}%; pass "
                                     "duty_percent")
                summary = (f"calibration pin will output 3.3 V square wave, {fmt_si(fs, 'Hz', 6)}, {du:.1f} % duty. "
                           "If the pin is wired into the detector, this switches real coil current.")
                req = sess.confirm("scope_generator", args, confirm_token, summary)
                if req is not None:
                    return req
            notes = sess.ensure_control()
            g = s.set_gen(enable=enabled, freq_hz=f, duty=None if duty_percent is None else duty_percent / 100)
            return {"ok": True, "applied": {"enabled": bool(g.enable), "frequency": _r6(g.frequency_hz),
                                            "duty_percent": _r6(g.duty * 100), "phase_inc": g.phase_inc},
                    "caveats": notes + [CAL_OUTPUT_NOTE, "Square wave only, 3.3 V logic, about 20 ns edge jitter. Not a sine source "
                                        "and not a power driver: buffer it before driving a MOSFET gate."]}
        return await run("scope_generator", 3, args, go)

    @register(3, title="Loopback self-test", destructive=True, open_world=True)
    async def scope_self_test_loopback(
        confirm_token: Annotated[str | None, Field(description="token from the confirmation response")] = None,
    ) -> dict[str, Any]:
        """Self-test with the calibration pin connected to CH1 by the probe (ask the user to connect it first and
        to disconnect anything else from the pin). Runs the generator at 10 kHz / 50 % and 1 MHz / 25 %,
        captures at several rates, checks frequency, duty and levels, and runs the protocol bench tests that need
        a known signal: T4 (A/B interleave order), T5 (trigger_addr alignment) and T7 (FORCE). Returns pass/fail
        per check. Restores the previous channel/timebase/trigger/generator settings. Use after every firmware or
        bitstream change. Needs a confirmation token (drives the calibration pin)."""
        args: dict[str, Any] = {}

        def go() -> dict[str, Any]:
            s = sess.get_scope()
            if not s.profile().has_generator:
                raise ToolError("scope_self_test_loopback needs the programmable generator (Atlan4 bitstream)")
            req = sess.confirm("scope_self_test_loopback", args, confirm_token,
                               "calibration pin will output 3.3 V square waves (10 kHz/50 %, 1 MHz/25 %) into CH1; "
                               "CH1 settings are changed during the test and restored afterwards")
            if req is not None:
                return req
            notes = sess.ensure_control()
            return _self_test(s, notes)
        return await run("scope_self_test_loopback", 3, args, go)

    @register(3, title="Bode / frequency-response sweep", destructive=True, open_world=True)
    async def scope_bode_sweep(
        ctx: Context,
        start: Annotated[float | str, Field(description="lowest frequency, e.g. '100Hz' (>= 1 Hz)")] = "100Hz",
        stop: Annotated[float | str, Field(description="highest frequency, e.g. '1MHz' (<= 10 MHz; above about "
                                                       "2 MHz the x1 probe on the post rolls the stimulus off)")] = "1MHz",
        points: Annotated[int, Field(ge=1, le=200, description="log-spaced points; requests that round to the "
                                                               "same clean-edge frequency are merged")] = 50,
        settle: Annotated[str | float, Field(description="periods before each record: 'fast' (3, RC / RL), 'normal' "
                                                         "(10, Q <= 7), 'resonant' (50, Q <= 34), 'high_q' (200), "
                                                         "or a number")] = "normal",
        ch2_range: Annotated[str | float, Field(description="'auto' (down-range CH2 as the response falls, splice "
                                                            "at each switch), 'fixed' (CH2's current range) or a "
                                                            "V/div at the probe tip")] = "auto",
        ch2_coupling: Annotated[Literal["dc", "ac"], Field(description="'ac' removes the DUT's DC level from CH2 "
                                "(wiring B puts the DUT 1.24 V below the probe reference, which saturates CH2 on 0.1 "
                                "and 0.2 V/div); about 1 Hz corner, take the thru with the same coupling")] = "dc",
        thru_cal: Annotated[str | None, Field(description="'measure': this sweep is a thru calibration (both "
                                                          "probes on the DUT input; stored under thru_name); a "
                                                          "stored name: divide this sweep by it")] = None,
        thru_name: Annotated[str | None, Field(description="name to store a measured thru calibration under "
                                                           "(default: a timestamp)")] = None,
        background: Annotated[bool, Field(description="one generator-off capture per rate and CH2 range: flags "
                                                      "points less than 20 dB above it 'near_floor'")] = True,
        max_duration_s: Annotated[float, Field(gt=0, le=3600, description="refuse sweeps estimated longer")] = 120.0,
        inline_max: Annotated[int, Field(ge=0, le=200, description="rows of the table returned inline")] = 200,
        where: Annotated[Literal["host", "device"], Field(description="'host': this server drives every capture "
                         "(any firmware with the filter); 'device': the scope runs the sweep itself (firmware 0.2.66+, "
                         "protocol 1.20; about 2-3 x faster, the person sees the Bode plot on the screen) and this "
                         "tool polls and reads its table. Same result structure, so the two compare point by "
                         "point")] = "host",
        close_view: Annotated[bool, Field(description="where='device': close the scope's Bode view afterwards "
                                                      "(gives the scope's settings back); default: it stays on the "
                                                      "screen")] = False,
        confirm_token: Annotated[str | None, Field(description="token from the confirmation response")] = None,
    ) -> dict[str, Any]:
        """Frequency response H(f) = CH2 / CH1 of a passive DUT driven by the calibration-pin generator (stepped
        single tone, docs/plans/bode-design.md). where='device' (firmware 0.2.66+): the scope's own Bode engine runs
        the same plan and rules (BODE_CTRL / READ_BODE) and this tool returns its table in the same structure; the
        device knows the settle presets only, ch2_range 'auto' or 'fixed', DC coupling; thru_cal there: 'measure'
        (stored on the scope and here), 'device' (the scope's stored thru) or a stored name (applied here). Wiring: post -> DUT input with the CH1 tip there, CH2 tip on the DUT
        output, DUT ground on the probe ground clips (wiring A; bench 2026-10-19: the same H as the USB-ground wiring
        within 0.4 dB). Wiring B (DUT ground on the USB shell, clips open) puts the DUT 1.24 V below the probe
        reference: use ch2_coupling='ac' there, or CH2 saturates on 0.1 and 0.2 V/div. Per point the generator
        runs at a clean-edge frequency 50 MHz / M, both channels are captured through the 16-bit CIC3 + FIR filter
        at a rate that makes the record a whole number of periods (legacy 200 MSa/s above 1.5 MHz), and a single-bin
        DFT at the exact DDS frequency gives magnitude (dB) and phase (deg); the square's harmonics drop out.

        Returns the plan, a table (f_requested, f_actual, mag_db, phase_deg, V1 / V2 rms, ranges, sample rate,
        filter, SNR, flags per point), markers (-3 dB points, peak and Q for a resonance, phase crossings), files
        (.csv, .json, .npz with the complex values) and caveats. Flagged points (clipped, near_floor, low_snr,
        unsettled, incoherent, ch1_clipped) are left out of the markers. Run thru_cal='measure' once with both
        probes on the DUT input, then pass its name to remove the CH1 / CH2 mismatch.

        Drives the calibration pin (3.1 Vpp square through 1 kOhm): the first call returns a confirmation token.
        Passive DUTs or buffered inputs only, never a coil, a MOSFET drain or anything that can drive the post.
        A CH2 saturated by the DUT's DC level reads flat: the point is flagged ch2_overload, auto mode steps CH2 up a
        range, a fixed range stops with the remedy.

        Changes the channels (coupling, ranges, offsets), time base, trigger, filter and generator during the
        sweep and gives them back afterwards."""
        args = {"start": start, "stop": stop, "points": points, "settle": settle, "ch2_range": ch2_range,
                "ch2_coupling": ch2_coupling, "thru_cal": thru_cal, "thru_name": thru_name, "background": background,
                "max_duration_s": max_duration_s, "inline_max": inline_max, "where": where, "close_view": close_view}
        on_device = where == "device"
        thru_dir = Path(cfg.data_dir) / "bode" / "thru"

        def safe(name: str) -> str:
            n = "".join(ch if ch.isalnum() or ch in "-_." else "_" for ch in name.strip()).strip(".")
            if not n:
                raise ValueError(f"thru calibration name {name!r} is empty")
            return n

        def known() -> list[str]:
            return sorted(p.stem for p in thru_dir.glob("*.json")) if thru_dir.is_dir() else []

        def go() -> dict[str, Any]:
            s = sess.get_scope()
            f0, f1 = parse_quantity(start, "Hz"), parse_quantity(stop, "Hz")
            plans = BD.plan_sweep(f0, f1, points)
            if f1 > min(cfg.gen_max_hz, 25e6) or f0 > min(cfg.gen_max_hz, 25e6):
                raise ValueError(f"stop {f1:g} Hz above the generator ceiling {cfg.gen_max_hz:g} Hz "
                                 "(FNIRSI_MCP_GEN_MAX_HZ)")
            settle_p = BD.parse_settle(settle)
            BD.BodeRunner._parse_ch2(ch2_range)  # validate before asking for a token
            if on_device:
                try:
                    BD.device_settle_code(settle)
                    BD.device_ch2_code(ch2_range)
                except ValueError as e:
                    raise ToolError(f"scope_bode_sweep(where='device'): {e}") from None
                if ch2_coupling != "dc":
                    raise ToolError("scope_bode_sweep(where='device'): the scope's sweep keeps CH2 DC coupled; use "
                                    "where='host' for ch2_coupling='ac'")
                if s.info.proto_minor < 20:
                    raise ToolError("scope_bode_sweep(where='device') needs firmware 0.2.66 or later (protocol 1.20, "
                                    f"this scope speaks 1.{s.info.proto_minor}); where='host' works here")
            cal = None
            if thru_cal is not None and thru_cal not in ("measure", "device" if on_device else "measure"):
                path = thru_dir / f"{safe(thru_cal)}.json"
                if not path.is_file():
                    raise ToolError(f"scope_bode_sweep: no thru calibration {thru_cal!r}; stored: "
                                    f"{', '.join(known()) or 'none'} (run with thru_cal='measure' first)")
                cal = BD.ThruCal.load(path)
            est = BD.estimate_duration(plans, settle_p, background, str(ch2_range).lower() == "auto")
            if est > max_duration_s:
                raise ToolError(f"scope_bode_sweep: estimated {est:.0f} s > max_duration_s {max_duration_s:g} s "
                                f"({len(plans)} points, lowest {plans[0].f_actual:.4g} Hz: the low decade "
                                "dominates); raise max_duration_s, raise start or use fewer points")
            if not s.profile().has_generator:
                raise ToolError("scope_bode_sweep: this FPGA bitstream has no programmable generator")
            if not s.info.caps & P.Caps.DECIM:
                raise ToolError("scope_bode_sweep: needs the decimation filter (FPGA 0x1534+, best 0x1535 with the "
                                "FIR); this bitstream would alias the stimulus harmonics at the slower rates")
            summary = (("the scope runs the sweep itself and shows its Bode plot: " if on_device else "") +
                       f"calibration pin will output a 3.1 Vpp square (through 1 kOhm) stepped over {len(plans)} "
                       f"frequencies {fmt_si(plans[0].f_actual, 'Hz', 4)} .. {fmt_si(plans[-1].f_actual, 'Hz', 4)}, "
                       f"about {est:.0f} s; both channels set to DC coupling, ranges and offsets changed and "
                       "restored afterwards. Only for a passive DUT (or a buffered input) on the post: if the pin is "
                       "wired into the detector, this switches real coil current.")
            req = sess.confirm("scope_bode_sweep", args, confirm_token, summary)
            if req is not None:
                return req
            notes = sess.ensure_control()

            def progress(i: int, n: int, text: str) -> None:
                try:
                    anyio.from_thread.run(_progress, ctx, i, n, text)
                except Exception:  # noqa: BLE001 - progress is best effort
                    pass

            if on_device:
                try:
                    res = BD.device_sweep(s, f0, f1, points, settle=settle, ch2_range=str(ch2_range).lower(),
                                          background=background, thru=cal if cal is not None else thru_cal,
                                          close_view=close_view, progress=progress)
                except BD.BodeError as e:
                    raise ToolError(f"scope_bode_sweep(where='device'): {e}") from None
                s.invalidate()  # the scope's sweep changed the channels, the time base and the trigger
            else:
                runner = BD.BodeRunner(s, settle=settle_p, ch2_range=ch2_range, background=background,
                                       relay_wait=sess.relay_slot, progress=progress, ch2_coupling=ch2_coupling)
                res = runner.run(plans, cal, role="thru" if thru_cal == "measure" else "measure")
            out: dict[str, Any] = {"ok": True, "where": where}
            if not sess.bode_wiring_shown:
                out["connection"] = BD.CONNECTION_TEXT
                sess.bode_wiring_shown = True
            rows = res.table()
            ps = BD.plan_summary(plans)
            out.update({
                "points": len(res.points), "planned": len(plans), "stopped_early": res.stopped_early,
                "plan": {**ps, "sample_rates": [_r6(x) for x in ps["sample_rates"]],
                         "settle_periods": settle_p, "ch2_range": res.ch2_mode},
                "summary": res.summary(), "table": rows[:inline_max],
                "table_truncated": len(rows) > inline_max,
                "ranges": {"start": res.meta.get("start_ranges"), "switches": res.range_switches,
                           "splices": res.splices},
                "timing": {"duration_s": round(res.duration_s, 2), "estimate_s": round(res.estimate_s, 2)},
            })
            if on_device:
                out["device"] = {k: res.meta.get(k) for k in ("sweep_id", "state", "reason", "thru", "thru_applied",
                                                              "view_shown")}
            now = _dt.datetime.now()
            d = Path(cfg.data_dir) / now.strftime("%Y-%m-%d")
            stem = (f"{now.strftime('%H%M%S_%f')[:-3]}_bode{'_device' if on_device else ''}"
                    f"{'_thru' if thru_cal == 'measure' else ''}")
            files = {"csv": res.write_csv(d / f"{stem}.csv"), "json": res.write_json(d / f"{stem}.json")}
            meta = {**res.to_json(), "table": None, "kind": "bode", "args": args, "json": files["json"]}
            files["npz"] = sess.save_npz("bode", res.arrays(), meta)
            out["files"] = files
            if thru_cal == "measure":
                name = safe(thru_name) if thru_name else now.strftime("thru_%Y%m%d_%H%M%S")
                tc = BD.ThruCal.from_sweep(res, name, tuple(res.meta["probes"]))
                out["thru_cal"] = {"stored_as": name, "file": tc.save(thru_dir / f"{name}.json"),
                                   "use": f"pass thru_cal='{name}' with the same start, stop and points" +
                                          (" (or thru_cal='device' for the copy the scope keeps until power-off)"
                                           if on_device else ""),
                                   "mismatch_db": [round(BD.db(abs(p.h_raw)), 4) for p in res.points],
                                   "mismatch_deg": [round(p.phase_deg, 3) for p in res.points]}
            elif cal is not None:
                out["thru_cal"] = {"applied": cal.name, "created": cal.created}
            elif thru_cal == "device":
                out["thru_cal"] = {"applied": "the scope's stored thru", "state": res.meta.get("thru")}
            cav = list(notes) + res.caveats + BD.result_caveats(res)
            if res.role == "thru":
                cav.append("thru calibration: both probes must have been on the DUT input node; |H| should be "
                           "within a few % of 0 dB (the CH1 / CH2 mismatch), otherwise the wiring was wrong")
            if not out.get("connection"):
                cav.append("wiring: DUT ground on the probe ground clips (wiring A), or on the USB ground with the "
                           "clips open and ch2_coupling='ac' (see the connection text of the first sweep)")
            out["caveats"] = cav
            out["applied"] = {"points": len(res.points), "f_first": plans[0].f_actual, "f_last": plans[-1].f_actual}
            return out
        return await run("scope_bode_sweep", 3, args, go)

    # ---- tier 4: raw ---------------------------------------------------------------------------------------

    @register(4, title="Raw FPGA bus access", destructive=True)
    async def scope_raw_fpga(
        ops: Annotated[list[dict[str, Any]], Field(description="operations in order: {'cmd': 0x14} writes a "
                       "command byte, {'write': [bytes]} writes data bytes, {'read': n} reads n bytes (1..256)")],
        confirm_token: Annotated[str | None, Field(description="token from the confirmation response")] = None,
    ) -> dict[str, Any]:
        """Raw FPGA bus operations (DEBUG_FPGA, debug firmware builds only) for bring-up and FPGA experiments.
        Returns the bytes read.

        Bypasses all validation. Wrong writes to 0x33/0x36 (relays) or 0x50-0x52 (generator) change hardware;
        0x01 resets acquisition."""
        args = {"ops": ops}

        def go() -> dict[str, Any]:
            s = sess.get_scope()
            if not s.profile().has(P.Caps.DEBUG_RAW):
                raise ToolError("scope_raw_fpga: firmware has no DEBUG_RAW capability (debug builds only)")
            enc: list[tuple[int, bytes | int]] = []
            for op in ops:
                if "cmd" in op:
                    enc.append((P.DBG_WRITE_CMD, int(op["cmd"])))
                elif "write" in op:
                    enc.append((P.DBG_WRITE_DATA, bytes(int(x) & 0xFF for x in op["write"])))
                elif "read" in op:
                    enc.append((P.DBG_READ_DATA, int(op["read"])))
                else:
                    raise ValueError(f"unknown op {op}; use cmd, write or read")
            summary = f"raw FPGA bus: {len(enc)} operations, unvalidated: " + ", ".join(
                f"{('cmd', 'write', 'read')[o - 1]} {a if isinstance(a, int) else a.hex()}" for o, a in enc)[:300]
            req = sess.confirm("scope_raw_fpga", args, confirm_token, summary)
            if req is not None:
                return req
            notes = sess.ensure_control()
            data = s.debug_fpga(enc)
            s.invalidate()
            return {"ok": True, "read": list(data), "read_hex": data.hex(" "), "applied": {"n_ops": len(enc)},
                    "caveats": notes + ["Bypasses all validation; cached configuration was invalidated."]}
        return await run("scope_raw_fpga", 4, args, go)

    @register(4, title="Reboot into FEL", destructive=True)
    async def scope_enter_fel(
        confirm_token: Annotated[str | None, Field(description="token from the confirmation response")] = None,
    ) -> dict[str, Any]:
        """Reboot the scope into the boot ROM's FEL mode so a new image can be loaded with xfel. The serial port
        disappears; the server drops the connection."""
        def go() -> dict[str, Any]:
            s = sess.get_scope()
            if not s.profile().has(P.Caps.ENTER_FEL):
                raise ToolError("scope_enter_fel: firmware has no ENTER_FEL capability")
            req = sess.confirm("scope_enter_fel", {}, confirm_token,
                               "scope reboots into FEL; the USB serial port disappears until it is power-cycled or "
                               "a new image is loaded with xfel")
            if req is not None:
                return req
            s.enter_fel()
            sess.drop_connection()
            sess.expect_remote = False
            return {"ok": True, "caveats": ["serial port gone; load an image with xfel, then reconnect"]}
        return await run("scope_enter_fel", 4, {}, go)

    return mcp, sess


async def _progress(ctx: Context | None, done: float, total: float, message: str | None = None) -> None:
    """Best-effort progress notification; never fails the tool."""
    if ctx is None:
        return
    try:
        await ctx.report_progress(done, total, message)
    except Exception:  # noqa: BLE001
        pass


def _slice_capture(cap: Capture, a: int, n: int) -> Capture:
    from dataclasses import replace

    off = a - cap.start_sample
    chans = {k: replace(v, codes=v.codes[off: off + n], volts=v.volts[off: off + n], clipped=v.clipped[off: off + n])
             for k, v in cap.channels.items()}
    return replace(cap, start_sample=a, channels=chans)


def _self_test(s: Scope, notes: list[str]) -> dict[str, Any]:
    """Loopback checks with the calibration pin on CH1 (docs/mcp-tools.md 4.4)."""
    saved_ch = s.get_channel(0, cached=False)
    saved_tb = s.get_timebase(cached=False)
    saved_tr = s.get_trigger(cached=False)
    saved_gen = s.get_gen()
    checks: list[dict[str, Any]] = []

    def check(name: str, passed: bool, **detail: Any) -> None:
        checks.append({"check": name, "pass": bool(passed), **{k: _r6(v) if isinstance(v, float) else v
                                                               for k, v in detail.items()}})
    try:
        s.set_channel(0, enable=True, range_index=1, coupling=0, dcoffset=0, probe_index=1)  # 2.5 V/div at 1x
        cfg = s.get_channel(0)
        mid = int(round(s.scale(0, cfg).raw_a_for_volts(1.65)))
        prof = s.profile()
        total = 8000 if prof.programmable else 3000  # >= 4 periods of 10 kHz at 20 MS/s
        pre = total // 2
        s.set_trigger(source=0, edge=0, level=mid, mode=1, pretrigger_samples=pre, total_samples=total)
        for f_hz, duty, rates in ((10e3, 0.5, (2e6, 20e6)), (1e6, 0.25, (50e6, 200e6))):
            s.set_gen(enable=True, freq_hz=f_hz, duty=duty)
            for fs in rates:
                n = max(0, int(P.BASE_CLOCK_HZ // fs) - 1)
                s.set_timebase(divider=n)
                cap = s.capture(["CH1"], 1.0, keep_raw=True)
                if cap is None:
                    check(f"capture {fmt_si(f_hz, 'Hz')} at {fmt_si(fs, 'S/s')}", False, reason="no trigger")
                    continue
                v = cap.channels["CH1"].volts
                fr = A.frequency(v, cap.dt)
                st = A.stats(v)
                fm = fr["frequency"] or 0.0
                check(f"frequency {fmt_si(f_hz, 'Hz')} at {fmt_si(fs, 'S/s')}", abs(fm - f_hz) / f_hz < 0.02,
                      measured=fm, expected=f_hz)
                if fs / f_hz >= 20:
                    dm = fr["duty"] or 0.0
                    check(f"duty {duty * 100:.0f} % at {fmt_si(fs, 'S/s')}", abs(dm - duty) < 0.03, measured=dm,
                          expected=duty)
                # the calibration output's DC level is not 0 V at the probe (T8): check the swing only
                lo, hi = (float(x) for x in np.percentile(v, [5, 95]))  # the two levels, not noise or ringing
                vpp = hi - lo
                check(f"swing at {fmt_si(fs, 'S/s')}", 2.6 <= vpp <= 3.7, vpp=vpp, low=lo, high=hi,
                      expected="~3.1..3.3 Vpp (DC level not checked: the output is not ground-referenced)")
                if f_hz == 1e6 and fs == 200e6:
                    _t4_t5(s, cap, check)
        # T7: FORCE in normal mode with no signal
        s.set_gen(enable=False)
        if prof.has(P.Caps.FORCE_TRIGGER):
            s.set_trigger(mode=1)
            s.arm()
            t0 = time.monotonic()
            s.force()
            st = s.wait(1.0)
            dt_ms = (time.monotonic() - t0) * 1000
            check("T7 FORCE completes a normal-mode capture without signal", st.acq_state == P.AcqState.DONE,
                  elapsed_ms=dt_ms)
        else:
            check("T7 FORCE", False, reason="FORCE_TRIGGER capability not set; run the bench test manually")
    finally:
        try:
            s.set_gen(enable=bool(saved_gen.enable), freq_hz=saved_gen.frequency_hz, duty=saved_gen.duty)
        except (DeviceError, TransportError):
            pass
        s.set_channel(0, enable=bool(saved_ch.enable), range_index=saved_ch.range, coupling=saved_ch.coupling,
                      dcoffset=saved_ch.dcoffset, invert=bool(saved_ch.invert), probe_index=saved_ch.probe_index)
        s.set_timebase(divider=saved_tb.divider)
        kw: dict[str, Any] = dict(source=saved_tr.source, edge=saved_tr.edge, level=saved_tr.level, mode=saved_tr.mode)
        if s.profile().programmable:
            kw.update(pretrigger_samples=saved_tr.pretrigger_samples, total_samples=saved_tr.total_samples)
        s.set_trigger(**kw)
    passed = all(c["pass"] for c in checks)
    return {"ok": True, "passed": passed, "n_checks": len(checks), "n_failed": sum(not c["pass"] for c in checks),
            "checks": checks, "caveats": notes + [CAL_OUTPUT_NOTE, "Requires the calibration pin connected to CH1 with the probe at 1x; "
                                                  "failures without that connection are expected."]}


def _t4_t5(s: Scope, cap: Capture, check: Callable[..., None]) -> None:
    """T4: which core comes first in time (edges must be monotonic); T5: trigger_addr vs the crossing."""
    ch = cap.channels["CH1"]
    a, b = ch.raw_a, ch.raw_b
    if a is None or b is None:
        return
    cal = s.get_cal(0)
    a = np.clip(a.astype(np.int16) + cal.adc_a_comp, 0, 255).astype(float)
    b = np.clip(b.astype(np.int16) + cal.adc_b_comp, 0, 255).astype(float)
    mid = 0.5 * (np.percentile(a, 2) + np.percentile(a, 98))

    def score(stream: np.ndarray) -> int:
        """Count non-monotonic steps in the 3-sample neighbourhood of each crossing."""
        bad = 0
        for x in A.crossings(stream, mid, "rising"):
            i = int(x)
            seg = stream[max(0, i - 2): i + 4]
            bad += int(np.sum(np.diff(seg) < -3))
        return bad
    s_b = score(np.ravel(np.column_stack([b, a])))
    s_a = score(np.ravel(np.column_stack([a, b])))
    order = s.info.interleave_order
    measured = 0 if s_b <= s_a else 1
    check("T4 interleave order (rising edges monotonic)", measured == order,
          firmware_order="B before A" if order == 0 else "A before B",
          measured_order="B before A" if measured == 0 else "A before B", nonmonotonic_b_first=s_b,
          nonmonotonic_a_first=s_a)
    # T5: crossing of core A nearest to the trigger word
    pre_w = cap.pretrigger_samples // 2
    xa = A.crossings(a, s.get_trigger().level + cal.adc_a_comp, "rising")
    if xa.size:
        k = xa[np.argmin(np.abs(xa - pre_w))]
        offset = (int(math.floor(k)) + 1) - pre_w
        check("T5 trigger_addr alignment (crossing word - pretrigger_words)", abs(offset) <= 1, offset_words=offset)


def _png_from_rgb565(fb: np.ndarray) -> bytes:
    """Encode an (h, w) RGB565 array as PNG without external dependencies."""
    h, w = fb.shape
    fb = fb.astype(np.uint32)
    r = ((fb >> 11) & 0x1F) * 255 // 31
    g = ((fb >> 5) & 0x3F) * 255 // 63
    b = (fb & 0x1F) * 255 // 31
    rgb = np.stack([r, g, b], axis=-1).astype(np.uint8)
    raw = b"".join(b"\x00" + rgb[y].tobytes() for y in range(h))

    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def preload_native_modules() -> None:
    """Import modules that load DLLs before the stdio transport starts.

    On Windows, loading a DLL (scipy's BLAS, pyserial's setupapi) deadlocks while another thread has a pending
    synchronous ReadFile on the stdin pipe, which is exactly what the MCP stdio reader thread does. Importing
    them here, before ``mcp.run``, avoids the hang on the first decay fit or port scan."""
    import scipy.linalg  # noqa: F401
    import scipy.optimize  # noqa: F401
    try:
        import serial  # noqa: F401
        import serial.tools.list_ports  # noqa: F401
    except ImportError:  # pragma: no cover
        pass


def tool_list(cfg: ServerConfig) -> list[tuple[str, int, str]]:
    """(name, tier, title) of every tool ``cfg`` registers; builds the server without connecting."""
    import asyncio
    import re

    mcp, sess = build_server(cfg)
    try:
        tools = asyncio.run(mcp.list_tools())
    finally:
        sess.close()
    out = []
    for t in tools:
        m = re.search(r"Safety tier (\d)\.", t.description or "")
        if m is None:
            raise RuntimeError(f"tool {t.name} has no safety tier in its description")
        out.append((t.name, int(m.group(1)), t.title or ""))
    return out


def print_tools(cfg: ServerConfig) -> None:
    tools = tool_list(cfg)
    for name, tier, title in tools:
        print(f"{name:32s} tier {tier}  {title}")
    print(f"{len(tools)} tools (max tier {cfg.max_tier}{', raw enabled' if cfg.enable_raw else ''})")
    print(f"data dir:  {Path(cfg.data_dir).resolve()}")
    print(f"audit log: {Path(cfg.audit_log).resolve() if cfg.audit_log else 'off'}")


def main(argv: list[str] | None = None) -> None:
    ap = argparse.ArgumentParser(prog="fnirsi1013d-mcp", description="MCP server for the FNIRSI 1013D oscilloscope")
    ap.add_argument("--port", help="COM5, /dev/ttyACM0, tcp://localhost:5555 (simulator) or auto "
                                   "(env FNIRSI_MCP_PORT)")
    ap.add_argument("--max-tier", type=int, help="highest safety tier exposed (env FNIRSI_MCP_MAX_TIER, default 3)")
    ap.add_argument("--enable-raw", action="store_true", default=None, help="allow tier 4 (raw FPGA, FEL)")
    ap.add_argument("--home", type=Path, help="per-user directory for data/ and logs/audit.jsonl (env "
                    "FNIRSI_MCP_HOME, default ~/.fnirsi1013d)")
    ap.add_argument("--data-dir", type=Path, help="where .npz files, calibrations and Bode thru files go (env "
                    "FNIRSI_MCP_DATA_DIR, default <home>/data)")
    ap.add_argument("--audit-log", type=Path, help="JSONL audit log (env FNIRSI_MCP_AUDIT_LOG, default "
                    "<home>/logs/audit.jsonl; empty string in the env disables it)")
    ap.add_argument("--cal-file", type=Path, help="per-unit voltage calibration file (env FNIRSI_MCP_CAL_FILE; "
                    "default <data-dir>/cal/<serial>.json, loaded if present)")
    ap.add_argument("--max-response", type=int, help="largest response payload requested in one frame; larger "
                    "reads are split. Default: the device limit; "
                    "0 = always the device limit (env FNIRSI_MCP_MAX_RESPONSE)")
    ap.add_argument("--sim", metavar="SIM_ARGS", help="start an in-process simulator and connect to it, e.g. "
                    "--sim \"--variant al3 --signal decay:tau=80us\" (overrides --port; for development)")
    ap.add_argument("--transport", choices=("stdio", "streamable-http"), default="stdio")
    ap.add_argument("--list-tools", action="store_true", help="print the tools this configuration registers "
                    "(name, tier, title), the data dir and the audit log, and exit without opening a port")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args(argv)
    logging.basicConfig(level=logging.INFO if a.verbose else logging.WARNING, stream=sys.stderr,
                        format="%(asctime)s %(name)s %(levelname)s %(message)s")
    cfg = ServerConfig.from_env(home=a.home, port=a.port, max_tier=a.max_tier, enable_raw=a.enable_raw,
                                data_dir=a.data_dir, audit_log=a.audit_log, max_response=a.max_response,
                                cal_file=a.cal_file)
    if a.list_tools:
        print_tools(cfg)
        return
    preload_native_modules()
    sim = None
    if a.sim is not None:
        from .simulator import start_from_args

        sim = start_from_args(a.sim)
        cfg.port = sim.url
        log.warning("in-process simulator on %s (%s)", sim.url, a.sim)
    mcp, sess = build_server(cfg)
    try:
        mcp.run(transport=a.transport)
    finally:
        sess.close()
        if sim is not None:
            sim.stop()


if __name__ == "__main__":
    main()
