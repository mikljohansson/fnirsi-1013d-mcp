"""Byte transport and request/response engine (docs/protocol.md sections 3, 4, 10).

Links:
    ``tcp://host:port``  the simulator (``fnirsi1013d-sim``) or any TCP bridge
    ``COM5``, ``/dev/ttyACM0``  a real scope over USB CDC ACM (pyserial)
    ``auto``  discovery by VID/PID and the ``#VER`` probe

Flow-control rules implemented here (section 3.2):

- one normal request in flight; while a deferred request (WAIT, ACQUIRE_AVG) is pending, at most one
  *control* request (GET_STATUS, STOP, SET_REMOTE, PING) may be sent in addition;
- every request has a response timeout; on timeout the same frame (same seq) is resent, which the firmware's
  duplicate cache answers without re-executing (section 4.3);
- after repeated failures the recovery procedure runs: stop sending, wait 150 ms, flush input, PING with a
  fresh sequence number;
- the parser resynchronises on garbage and drops partial frames after 100 ms of silence.
"""

from __future__ import annotations

import logging
import re
import socket
import threading
import time
from dataclasses import dataclass
from typing import Callable, Protocol

from . import protocol as P

log = logging.getLogger(__name__)

#: Development VID/PID (docs/plans/firmware-usb-protocol.md section 10): pid.codes test range.
PIDCODES_VID = 0x1209
PIDCODES_TEST_PIDS = range(0x0001, 0x0011)
#: Current (1.00o5) ST Virtual COM Port IDs; only accepted after a successful #VER probe.
ST_VID, ST_PID = 0x0483, 0x5740
PRODUCT_STRING = "FNIRSI 1013D"
VER_PREFIX = "#OK,FNIRSI-1013D-AI"

WRITE_TIMEOUT_S = 2.0
DEFAULT_RESPONSE_TIMEOUT_S = 1.0
RECOVERY_PAUSE_S = 0.150


class TransportError(Exception):
    """Link-level failure: port gone, timeouts after retries, protocol version mismatch."""


class DeviceError(Exception):
    """The device answered with status != OK (section 9)."""

    def __init__(self, cmd: int, status: int, detail: int = 0, text: str = ""):
        self.cmd = cmd
        self.status = status
        self.detail = detail
        self.text = text
        try:
            sname = P.Status(status).name
        except ValueError:
            sname = f"status {status}"
        try:
            cname = P.Cmd(cmd).name
        except ValueError:
            cname = f"0x{cmd:04X}"
        hint = P.STATUS_HINTS.get(status, "")
        msg = f"{cname} failed: {sname}"
        if text:
            msg += f" ({text})"
        if detail:
            msg += f" [detail {detail}]"
        if hint:
            msg += f". {hint}"
        super().__init__(msg)


class Link(Protocol):
    def write(self, data: bytes) -> None: ...
    def read(self, max_bytes: int, timeout: float) -> bytes: ...
    def flush_input(self) -> None: ...
    def close(self) -> None: ...
    @property
    def description(self) -> str: ...


class TcpLink:
    """TCP stream to the simulator. Closing the socket is the DTR 1->0 equivalent."""

    def __init__(self, host: str, port: int, connect_timeout: float = 3.0):
        # Windows resolves "localhost" to ::1 first; the simulator listens on IPv4, which would cost a 2 s
        # connect timeout per attempt.
        addr = "127.0.0.1" if host.lower() == "localhost" else host
        try:
            self._sock = socket.create_connection((addr, port), timeout=connect_timeout)
        except OSError as e:
            raise TransportError(f"cannot connect to tcp://{host}:{port}: {e}. Is the simulator running "
                                 f"(fnirsi1013d-sim --port {port})?") from e
        self._sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._desc = f"tcp://{host}:{port}"

    @property
    def description(self) -> str:
        return self._desc

    def write(self, data: bytes) -> None:
        self._sock.settimeout(WRITE_TIMEOUT_S)
        try:
            self._sock.sendall(data)
        except (socket.timeout, OSError) as e:
            raise TransportError(f"write to {self._desc} failed: {e}") from e

    def read(self, max_bytes: int, timeout: float) -> bytes:
        self._sock.settimeout(max(timeout, 0.0) or 0.0001)
        try:
            data = self._sock.recv(max_bytes)
        except socket.timeout:
            return b""
        except OSError as e:
            raise TransportError(f"read from {self._desc} failed: {e}") from e
        if not data:
            raise TransportError(f"{self._desc} closed by the device")
        return data

    def flush_input(self) -> None:
        self._sock.setblocking(False)
        try:
            while self._sock.recv(65536):
                pass
        except (BlockingIOError, OSError):
            pass
        finally:
            self._sock.setblocking(True)

    def close(self) -> None:
        try:
            self._sock.close()
        except OSError:
            pass


class SerialLink:
    """USB CDC ACM via pyserial. Baud rate is ignored by the device; DTR must be asserted (section 3.1)."""

    def __init__(self, port: str):
        import serial  # imported lazily so the simulator/tests do not need a serial stack

        self._serial_mod = serial
        try:
            self._ser = serial.Serial(port=None, baudrate=115200, timeout=0.05, write_timeout=WRITE_TIMEOUT_S)
            self._ser.port = port
            self._ser.dtr = True
            self._ser.rts = True
            self._ser.open()
        except serial.SerialException as e:
            raise TransportError(f"cannot open {port}: {e}") from e
        self._desc = port

    @property
    def description(self) -> str:
        return self._desc

    def write(self, data: bytes) -> None:
        try:
            self._ser.write(data)
        except self._serial_mod.SerialTimeoutException as e:
            raise TransportError(f"write to {self._desc} timed out after {WRITE_TIMEOUT_S} s: scope hung "
                                 "or not reading") from e
        except self._serial_mod.SerialException as e:
            raise TransportError(f"write to {self._desc} failed: {e}") from e

    def read(self, max_bytes: int, timeout: float) -> bytes:
        try:
            self._ser.timeout = max(timeout, 0.0)
            n = self._ser.in_waiting
            return self._ser.read(min(max(n, 1), max_bytes))
        except self._serial_mod.SerialException as e:
            raise TransportError(f"read from {self._desc} failed: {e}") from e

    def flush_input(self) -> None:
        try:
            self._ser.reset_input_buffer()
        except self._serial_mod.SerialException:
            pass

    def close(self) -> None:
        try:
            self._ser.close()
        except Exception:  # noqa: BLE001
            pass


def open_link(url: str) -> Link:
    """Open ``tcp://host:port``, a serial port name, or ``auto``."""
    url = url.strip()
    if url.lower().startswith("tcp://"):
        m = re.fullmatch(r"tcp://\[?([^\]]+?)\]?:(\d+)", url, flags=re.IGNORECASE)
        if not m:
            raise TransportError(f"bad TCP url {url!r}; expected tcp://host:port")
        return TcpLink(m.group(1), int(m.group(2)))
    if url.lower() in ("", "auto"):
        port = discover_port()
        return SerialLink(port)
    return SerialLink(url)


# ---------------------------------------------------------------------------------------------------------
# Discovery


@dataclass
class PortCandidate:
    device: str
    vid: int | None
    pid: int | None
    product: str
    reason: str


def list_candidates() -> list[PortCandidate]:
    """Serial ports that may be a 1013D, best first: pid.codes test VID/PID or the product string, then the
    ST VCP IDs that 1.00o5-derived firmware still uses."""
    from serial.tools import list_ports

    strong: list[PortCandidate] = []
    weak: list[PortCandidate] = []
    for p in list_ports.comports():
        product = (p.product or p.description or "")
        if PRODUCT_STRING.lower() in product.lower():
            strong.append(PortCandidate(p.device, p.vid, p.pid, product, "product string"))
        elif p.vid == PIDCODES_VID and p.pid in PIDCODES_TEST_PIDS:
            strong.append(PortCandidate(p.device, p.vid, p.pid, product, "pid.codes test VID/PID"))
        elif p.vid == ST_VID and p.pid == ST_PID:
            weak.append(PortCandidate(p.device, p.vid, p.pid, product, "ST VCP VID/PID (needs #VER)"))
    return strong + weak


def probe_ver(link: Link, timeout: float = 0.5) -> str | None:
    """Send the ASCII ``#VER`` probe (section 10) and return the reply line, or None."""
    link.flush_input()
    link.write(b"#VER\n")
    parser = P.FrameParser()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        data = link.read(256, deadline - time.monotonic())
        for ev in parser.feed(data):
            if isinstance(ev, P.AsciiLine) and ev.text.startswith("#"):
                return ev.text
    return None


def parse_ver(line: str) -> dict[str, str]:
    """``#OK,FNIRSI-1013D-AI,fw=0.1.0,proto=1.0,fpga=0x1532`` -> {'fw': ..., 'proto': ..., 'fpga': ...}."""
    out: dict[str, str] = {}
    for part in line.split(",")[2:]:
        if "=" in part:
            k, v = part.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def discover_port() -> str:
    cands = list_candidates()
    tried = []
    for c in cands:
        try:
            link = SerialLink(c.device)
        except TransportError as e:
            tried.append(f"{c.device}: {e}")
            continue
        try:
            reply = probe_ver(link)
        finally:
            link.close()
        if reply and reply.startswith(VER_PREFIX):
            return c.device
        tried.append(f"{c.device} ({c.reason}): #VER reply {reply!r}")
    raise TransportError(
        "no FNIRSI 1013D found. Candidates: " + ("; ".join(tried) if tried else "none") +
        ". Connect the scope, check that the open firmware with USB control is running, or pass the port "
        "explicitly (e.g. --port COM5 or --port tcp://localhost:5555 for the simulator).")


# ---------------------------------------------------------------------------------------------------------
# Request engine


@dataclass
class LinkStats:
    frames_sent: int = 0
    frames_received: int = 0
    retries: int = 0
    recoveries: int = 0
    garbage_bytes: int = 0
    bad_header: int = 0
    bad_payload_crc: int = 0
    partial_drops: int = 0
    stale_responses: int = 0


class Transport:
    """Thread-safe request/response engine over a Link."""

    def __init__(self, link: Link, retries: int = 2, response_timeout: float = DEFAULT_RESPONSE_TIMEOUT_S):
        self.link = link
        self.retries = retries
        self.response_timeout = response_timeout
        self.stats = LinkStats()
        self._parser = P.FrameParser()
        self._lock = threading.RLock()
        self._seq = 0
        self._inbox: dict[int, P.Frame] = {}
        self._outstanding: dict[int, int] = {}  # seq -> cmd
        self._deferred_seq: int | None = None
        self._last_rx = time.monotonic()
        self.ascii_lines: list[str] = []
        self.last_activity = time.monotonic()

    # --- low level -------------------------------------------------------------------------------------

    def close(self) -> None:
        self.link.close()

    def next_seq(self) -> int:
        with self._lock:
            self._seq = (self._seq + 1) & 0xFFFF
            return self._seq

    def _send(self, frame: P.Frame) -> None:
        data = frame.encode()
        if frame.payload and len(data) > P.MAX_REQUEST_FRAME:
            raise TransportError(f"{frame.cmd_name()} request is {len(data)} bytes; the limit is "
                                 f"{P.MAX_REQUEST_FRAME}")
        self.link.write(data)
        self.stats.frames_sent += 1
        self.last_activity = time.monotonic()

    def _pump(self, timeout: float) -> None:
        """Read once (up to ``timeout``) and dispatch every complete event."""
        data = self.link.read(65536, timeout)
        now = time.monotonic()
        if not data:
            if self._parser.pending_bytes and now - self._last_rx > P.INTERBYTE_TIMEOUT_S:
                self._parser.reset()
                self.stats.partial_drops += 1
            return
        self._last_rx = now
        for ev in self._parser.feed(data):
            if isinstance(ev, P.Frame):
                if not ev.is_response or ev.proto_major != P.PROTO_MAJOR:
                    self.stats.garbage_bytes += len(ev)
                    continue
                self.stats.frames_received += 1
                if self._outstanding.get(ev.seq) == ev.cmd:
                    self._inbox[ev.seq] = ev
                else:
                    self.stats.stale_responses += 1
            elif isinstance(ev, P.Garbage):
                self.stats.garbage_bytes += len(ev.data)
            elif isinstance(ev, P.BadHeader):
                self.stats.bad_header += 1
            elif isinstance(ev, (P.BadPayloadCrc, P.Oversize)):
                self.stats.bad_payload_crc += 1
            elif isinstance(ev, P.AsciiLine):
                self.ascii_lines.append(ev.text)

    def _await(self, seq: int, deadline: float, cancel: Callable[[], bool] | None = None,
               poll: float = 0.05) -> P.Frame | None:
        while True:
            if seq in self._inbox:
                return self._inbox.pop(seq)
            now = time.monotonic()
            if now >= deadline:
                return None
            if cancel is not None and cancel():
                return None
            self._pump(min(deadline - now, poll))

    def recover(self) -> None:
        """Host recovery procedure (section 3.2)."""
        with self._lock:
            self.stats.recoveries += 1
            time.sleep(RECOVERY_PAUSE_S)
            self.link.flush_input()
            self._parser.reset()
            self._inbox.clear()
            keep = {self._deferred_seq: self._outstanding[self._deferred_seq]} \
                if self._deferred_seq in self._outstanding else {}
            self._outstanding = keep
            self._request(P.Cmd.PING, b"recover", 0.5, retries=0)

    # --- requests --------------------------------------------------------------------------------------

    def request(self, cmd: int, payload: bytes = b"", timeout: float | None = None, *, check: bool = True,
                retries: int | None = None) -> P.Frame:
        """Send one request and return its response. Raises DeviceError if status != OK and ``check``."""
        with self._lock:
            if self._deferred_seq is not None and cmd not in P.CONTROL_CMDS:
                raise TransportError(f"{P.Cmd(cmd).name} not allowed while a deferred request is pending; "
                                     "only GET_STATUS, STOP, SET_REMOTE and PING are")
            try:
                return self._request(cmd, payload, timeout, retries=retries, check=check)
            except TransportError:
                if retries == 0:
                    raise
                # one recovery attempt, then a single fresh try
                self.recover()
                return self._request(cmd, payload, timeout, retries=0, check=check)

    def _request(self, cmd: int, payload: bytes, timeout: float | None, *, retries: int | None = None,
                 check: bool = True) -> P.Frame:
        seq = self.next_seq()
        frame = P.Frame(cmd, seq, payload)
        tmo = self.response_timeout if timeout is None else timeout
        n_try = 1 + (self.retries if retries is None else retries)
        self._outstanding[seq] = cmd
        try:
            for attempt in range(n_try):
                if attempt:
                    self.stats.retries += 1
                self._send(frame)
                resp = self._await(seq, time.monotonic() + tmo)
                if resp is None:
                    continue
                if resp.status == P.Status.E_BAD_CRC and attempt + 1 < n_try:
                    continue  # our request was corrupted on the way; resend
                return self._check(resp) if check else resp
            raise TransportError(f"no response to {P.Cmd(cmd).name} (seq {seq}) from {self.link.description} "
                                 f"after {n_try} attempts of {tmo:.2f} s")
        finally:
            self._outstanding.pop(seq, None)

    @staticmethod
    def _check(resp: P.Frame) -> P.Frame:
        if resp.status != P.Status.OK:
            detail, text = P.parse_error_payload(resp.payload)
            raise DeviceError(resp.cmd, resp.status, detail, text)
        return resp

    def deferred(self, cmd: int, payload: bytes, timeout: float,
                 cancel: Callable[[], bool] | None = None, on_cancel: Callable[[], None] | None = None,
                 check: bool = True) -> P.Frame:
        """Run a deferred request (WAIT, ACQUIRE_AVG). While it is pending, ``cancel()`` is polled; when it
        returns true a STOP control request is sent (``on_cancel`` may send something else) and the deferred
        response, normally ``E_ABORTED``, is collected."""
        with self._lock:
            if self._deferred_seq is not None:
                raise TransportError("a deferred request is already pending")
            seq = self.next_seq()
            frame = P.Frame(cmd, seq, payload)
            self._outstanding[seq] = cmd
            self._deferred_seq = seq
            try:
                self._send(frame)
                deadline = time.monotonic() + timeout
                resp = self._await(seq, deadline, cancel)
                if resp is None and cancel is not None and cancel():
                    if on_cancel:
                        on_cancel()
                    else:
                        self._request(P.Cmd.STOP, b"", None, check=False)
                    resp = self._await(seq, time.monotonic() + 1.0)
                if resp is None:
                    # Retry once with the same seq: the duplicate cache will answer if it completed.
                    self.stats.retries += 1
                    self._send(frame)
                    resp = self._await(seq, time.monotonic() + 1.0)
                if resp is None:
                    raise TransportError(f"no response to deferred {P.Cmd(cmd).name} within {timeout:.1f} s")
                return self._check(resp) if check else resp
            finally:
                self._outstanding.pop(seq, None)
                self._deferred_seq = None

    def control(self, cmd: int, payload: bytes = b"", timeout: float | None = None) -> P.Frame:
        """Send a control request while a deferred one is pending. Call it from the ``cancel`` / ``on_cancel``
        callbacks of :meth:`deferred` (same thread; the engine is single-reader by design)."""
        if cmd not in P.CONTROL_CMDS:
            raise TransportError(f"{P.Cmd(cmd).name} is not a control request")
        with self._lock:
            return self._request(cmd, payload, timeout)

    # --- ASCII -----------------------------------------------------------------------------------------

    def ascii(self, line: str, timeout: float = 0.5) -> str | None:
        with self._lock:
            n0 = len(self.ascii_lines)
            self.link.write(line.encode("ascii") + b"\n")
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if len(self.ascii_lines) > n0:
                    return self.ascii_lines.pop(n0)
                self._pump(min(0.05, deadline - time.monotonic()))
            return None


def connect(url: str, **kw: object) -> Transport:
    return Transport(open_link(url), **kw)  # type: ignore[arg-type]
