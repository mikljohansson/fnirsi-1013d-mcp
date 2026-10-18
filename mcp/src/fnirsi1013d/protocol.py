"""Wire format of the FNIRSI 1013D PC control protocol v1.0 (docs/protocol.md).

Everything here is pure: no I/O. The transport, the simulator and the tests all share this module, so the
host and the simulated device can never disagree about the encoding.

Section numbers in comments refer to docs/protocol.md.
"""

from __future__ import annotations

import binascii
import dataclasses
import enum
import struct
import zlib
from dataclasses import dataclass, field
from typing import Any, ClassVar, Iterator

# ---------------------------------------------------------------------------------------------------------
# Constants (section 4)

MAGIC = b"\xa5\x13"
MAGIC0 = 0xA5
MAGIC1 = 0x13
PROTO_MAJOR = 1
#: 1.1: GET_STATUS pass_ms / pass_max_ms (fw 0.1.1); 1.2: GET_LINK_STATS (fw 0.1.4); 1.3: collaborative (shared)
#: mode: SHARED_MODE cap, GET_STATUS settings/run fields, SET_ACQ_STATE, GET_CAPTURE_INFO
PROTO_MINOR = 3
HEADER_SIZE = 16
CRC_SIZE = 4
MAX_REQUEST_FRAME = 512
MAX_REQUEST_PAYLOAD = MAX_REQUEST_FRAME - HEADER_SIZE - CRC_SIZE  # 492
MAX_RESPONSE_PAYLOAD = 66560
#: Partial frames are discarded after this much silence (section 3.2).
INTERBYTE_TIMEOUT_S = 0.100

FLAG_RESPONSE = 0x01
FLAG_EVENT = 0x02

_HDR = struct.Struct("<BBBBHHHI")  # 14 bytes, then u16 hdr_crc


def crc16_ccitt_false(data: bytes) -> int:
    """CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, xorout 0)."""
    return binascii.crc_hqx(data, 0xFFFF)


def crc32_iso_hdlc(data: bytes) -> int:
    """CRC-32/ISO-HDLC, the zlib CRC."""
    return zlib.crc32(data) & 0xFFFFFFFF


class Cmd(enum.IntEnum):
    """Command ids (section 8.1)."""

    PING = 0x0001
    GET_INFO = 0x0002
    GET_STATUS = 0x0003
    SET_REMOTE = 0x0004
    GET_BATTERY = 0x0005
    TEST_PATTERN = 0x0006
    GET_LINK_STATS = 0x0007  # protocol 1.2
    GET_CHANNEL = 0x0100
    SET_CHANNEL = 0x0101
    GET_TIMEBASE = 0x0102
    SET_TIMEBASE = 0x0103
    GET_TRIGGER = 0x0104
    SET_TRIGGER = 0x0105
    ARM = 0x0200
    FORCE = 0x0201
    STOP = 0x0202
    WAIT = 0x0203
    READ_RAW = 0x0204
    READ_TRACE = 0x0205
    READ_LIVE = 0x0206
    ACQUIRE_AVG = 0x0207
    SET_ACQ_STATE = 0x0208  # protocol 1.3, SHARED_MODE
    GET_CAPTURE_INFO = 0x0209  # protocol 1.3, SHARED_MODE
    GET_GEN = 0x0300
    SET_GEN = 0x0301
    GET_CAL = 0x0400
    READ_SCREEN = 0x0401
    SD_INFO = 0x0500  # protocol 1.3, fw 0.2.2, cap SD_SECTORS
    SD_READ_SECTORS = 0x0501
    SD_STAGE = 0x0502
    SD_WRITE_SECTORS = 0x0503
    DEBUG_FPGA = 0x0F00
    ENTER_FEL = 0x0F01


#: Commands accepted while the UI owns the scope (section 8.1, column LOCAL).
LOCAL_ALLOWED = frozenset(
    {
        Cmd.PING, Cmd.GET_INFO, Cmd.GET_STATUS, Cmd.SET_REMOTE, Cmd.GET_BATTERY, Cmd.TEST_PATTERN,
        Cmd.GET_LINK_STATS,
        Cmd.GET_CHANNEL, Cmd.GET_TIMEBASE, Cmd.GET_TRIGGER, Cmd.READ_TRACE, Cmd.GET_GEN, Cmd.GET_CAL,
        Cmd.READ_SCREEN, Cmd.ENTER_FEL,
    }
)
#: Commands accepted while the UI is modal (LOCAL_BUSY, section 6.1).
BUSY_ALLOWED = frozenset({Cmd.PING, Cmd.GET_INFO, Cmd.GET_STATUS, Cmd.GET_BATTERY, Cmd.GET_LINK_STATS})
#: Control requests allowed while a deferred request is pending (section 3.2).
#: SET_ACQ_STATE (1.3) is a control request too: RUN/STOP while a WAIT is pending aborts it (E_ABORTED).
CONTROL_CMDS = frozenset({Cmd.GET_STATUS, Cmd.STOP, Cmd.SET_REMOTE, Cmd.PING, Cmd.SET_ACQ_STATE})
DEFERRED_CMDS = frozenset({Cmd.WAIT, Cmd.ACQUIRE_AVG})


class Status(enum.IntEnum):
    """Error codes (section 9)."""

    OK = 0
    E_BAD_CRC = 1
    E_BAD_LEN = 2
    E_UNKNOWN_CMD = 3
    E_BAD_ARG = 4
    E_UNSUPPORTED = 5
    E_NOT_REMOTE = 6
    E_BUSY = 7
    E_STATE = 8
    E_FPGA = 9
    E_ABORTED = 10
    E_VERSION = 11
    E_OVERFLOW = 12
    #: fw 0.2.2: SD card error; detail = negated driver error (2 timeout, 4 out of range) or 0xFFFF read-back mismatch
    E_IO = 13
    E_INTERNAL = 255


STATUS_HINTS: dict[int, str] = {
    Status.E_BAD_CRC: "payload CRC mismatch on the link; retry, check the cable",
    Status.E_BAD_LEN: "payload length wrong for the command",
    Status.E_UNKNOWN_CMD: "firmware does not know this command; check the firmware version",
    Status.E_BAD_ARG: "a field is out of range",
    Status.E_UNSUPPORTED: "not available on this FPGA bitstream or firmware build",
    Status.E_NOT_REMOTE: "command needs REMOTE ownership; acquire the remote session first",
    Status.E_BUSY: "scope UI is in a modal screen, or a deferred request is already pending",
    Status.E_STATE: "wrong acquisition state (e.g. read before the capture is DONE)",
    Status.E_FPGA: "FPGA did not become ready",
    Status.E_ABORTED: "request ended by STOP, release or watchdog",
    Status.E_VERSION: "protocol major version mismatch",
    Status.E_OVERFLOW: "response would exceed max_response_payload; read fewer words",
    Status.E_IO: "SD card error (detail: driver error, 0xFFFF = read-back mismatch); nothing past the failing chunk was written",
    Status.E_INTERNAL: "firmware bug",
}


class Caps(enum.IntFlag):
    """GET_INFO capability bits (section 8.2)."""

    PROG_TRIGGER_POS = 1 << 0
    GENERATOR_DDS = 1 << 1
    HOLDOFF = 1 << 2
    EXT_TRIGGER = 1 << 3
    TRIG_BOTH_CORES = 1 << 4
    FORCE_TRIGGER = 1 << 5
    SCREENSHOT = 1 << 6
    DEVICE_AVERAGE = 1 << 7
    DEBUG_RAW = 1 << 8
    LIVE_READ = 1 << 9
    ENTER_FEL = 1 << 10
    READ_LAG_UNVERIFIED = 1 << 11
    INTERLEAVE_UNVERIFIED = 1 << 12
    #: protocol 1.3: collaborative mode. No LOCAL/REMOTE; UI and host share one live settings set.
    SHARED_MODE = 1 << 13
    #: fw 0.2.2: SD_INFO / SD_READ_SECTORS / SD_STAGE / SD_WRITE_SECTORS
    SD_SECTORS = 1 << 14


def caps_names(caps: int) -> list[str]:
    return [c.name for c in Caps if caps & c and c.name]


#: Capability bit each command needs (section 8.1, column Cap).
CMD_CAP: dict[Cmd, Caps] = {
    Cmd.FORCE: Caps.FORCE_TRIGGER,
    Cmd.READ_LIVE: Caps.LIVE_READ,
    Cmd.ACQUIRE_AVG: Caps.DEVICE_AVERAGE,
    Cmd.SET_GEN: Caps.GENERATOR_DDS,
    Cmd.READ_SCREEN: Caps.SCREENSHOT,
    Cmd.DEBUG_FPGA: Caps.DEBUG_RAW,
    Cmd.ENTER_FEL: Caps.ENTER_FEL,
    Cmd.SET_ACQ_STATE: Caps.SHARED_MODE,
    Cmd.GET_CAPTURE_INFO: Caps.SHARED_MODE,
    Cmd.SD_INFO: Caps.SD_SECTORS,
    Cmd.SD_READ_SECTORS: Caps.SD_SECTORS,
    Cmd.SD_STAGE: Caps.SD_SECTORS,
    Cmd.SD_WRITE_SECTORS: Caps.SD_SECTORS,
}


class FpgaVariant(enum.IntEnum):
    UNKNOWN = 0
    STOCK = 1
    AL3 = 2
    EF2 = 3
    EP4 = 4


FPGA_VERSION_WORDS = {0x1432: FpgaVariant.STOCK, 0x1532: FpgaVariant.AL3, 0x1632: FpgaVariant.EF2,
                      0x1732: FpgaVariant.EP4}
VARIANT_NAMES = {FpgaVariant.UNKNOWN: "unknown", FpgaVariant.STOCK: "stock", FpgaVariant.AL3: "al3",
                 FpgaVariant.EF2: "ef2", FpgaVariant.EP4: "ep4"}


class Control(enum.IntEnum):
    LOCAL = 0
    REMOTE = 1
    #: protocol 1.3 collaborative mode (SHARED_MODE): always this value
    SHARED = 2


class RunState(enum.IntEnum):
    """GET_STATUS run_state (protocol 1.3): what the scope's acquisition loop does."""

    STOP = 0
    RUN = 1
    #: a single capture (host ARM or UI single) is armed and waiting
    SINGLE = 2


class CaptureOrigin(enum.IntEnum):
    """GET_STATUS capture_origin, GET_CAPTURE_INFO origin and READ_TRACE origin (protocol 1.3)."""

    HOST = 0
    UI_RUN = 1
    UI_SINGLE = 2


#: capture_settings_seq of a capture whose settings changed while it ran (and before the first capture);
#: settings_seq itself never takes this value (protocol 1.3)
SEQ_INVALID = 0xFFFFFFFF


class UiBusy(enum.IntEnum):
    """GET_STATUS ui_busy."""

    IDLE = 0
    MODAL = 1
    #: protocol 1.3 firmware: USB is up but the main loop has not finished its first pass
    STARTING = 2


class AcqState(enum.IntEnum):
    IDLE = 0
    ARMED = 1
    TRIGGERED = 2
    DONE = 3
    ABORTED = 4


class ExitReason(enum.IntEnum):
    NONE = 0
    HOST_RELEASE = 1
    USER_TAP = 2
    WATCHDOG = 3
    DTR_DROP = 4
    USB_RESET = 5


class StatusFlags(enum.IntFlag):
    FPGA_TRIGGERED = 1 << 0
    FPGA_DONE = 1 << 1
    AUTO_TIMEOUT = 1 << 2
    DTR = 1 << 3
    CHARGING = 1 << 4
    DEFERRED_PENDING = 1 << 5
    OVERLOAD_CH1 = 1 << 6
    OVERLOAD_CH2 = 1 << 7


# Static tables used by both host and simulator (sections 8.3, 8.5)

#: UI V/div per range index at a 1x probe.
RANGE_VOLTS_PER_DIV = (5.0, 2.5, 1.0, 0.5, 0.2, 0.1, 0.05)
#: probe_index -> factor (section 8.3).
PROBE_FACTORS = (0.5, 1.0, 10.0, 20.0, 50.0, 100.0, 1000.0)
#: UI time/div per timeperdiv_index (S100 variables.c:386 comments). Indices 0..10 are roll mode.
TIME_PER_DIV = (
    50, 20, 10, 5, 2, 1, 0.5, 0.2, 0.1, 0.05, 0.02,
    0.2, 0.1, 0.05, 0.02, 0.01, 0.005, 0.002, 0.001, 500e-6, 200e-6, 100e-6, 50e-6, 20e-6, 10e-6, 5e-6,
    2e-6, 1e-6, 500e-9, 200e-9, 100e-9, 50e-9, 20e-9, 10e-9, 5e-9,
)
#: time_per_div_sample_rate[] (S100 variables.c:386-401).
TIME_PER_DIV_SAMPLE_RATE = (
    28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18,
    17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, 0, 0, 0, 0, 0, 0,
)
#: sample_rate_settings[] (S100 variables.c:723-753), first 18 entries are the real ones.
SAMPLE_RATE_DIVIDERS = (0, 1, 3, 9, 19, 39, 99, 199, 399, 999, 1999, 3999, 9999, 19999, 39999, 99999,
                        199999, 399999)
MIN_REMOTE_TIMEPERDIV_INDEX = 11

BASE_CLOCK_HZ = 200_000_000
GEN_CLOCK_HZ = 50_000_000
ENTER_FEL_MAGIC = 0x4C45465F


# ---------------------------------------------------------------------------------------------------------
# Frames (section 4)


class ProtocolError(Exception):
    """Raised for malformed data that cannot be a valid frame."""


@dataclass(slots=True)
class Frame:
    cmd: int
    seq: int
    payload: bytes = b""
    status: int = 0
    flags: int = 0
    proto_major: int = PROTO_MAJOR

    @property
    def is_response(self) -> bool:
        return bool(self.flags & FLAG_RESPONSE)

    def header(self) -> bytes:
        h = _HDR.pack(MAGIC0, MAGIC1, self.proto_major, self.flags, self.cmd & 0xFFFF, self.seq & 0xFFFF,
                      self.status & 0xFFFF, len(self.payload))
        return h + struct.pack("<H", crc16_ccitt_false(h))

    def encode(self) -> bytes:
        out = self.header() + self.payload
        if self.payload:
            out += struct.pack("<I", crc32_iso_hdlc(self.payload))
        return out

    def __len__(self) -> int:
        return HEADER_SIZE + len(self.payload) + (CRC_SIZE if self.payload else 0)

    def cmd_name(self) -> str:
        try:
            return Cmd(self.cmd).name
        except ValueError:
            return f"0x{self.cmd:04X}"


def encode_frame(cmd: int, seq: int, payload: bytes = b"", status: int = 0, flags: int = 0,
                 proto_major: int = PROTO_MAJOR) -> bytes:
    return Frame(cmd, seq, payload, status, flags, proto_major).encode()


def request(cmd: int, seq: int, payload: bytes = b"") -> bytes:
    return encode_frame(cmd, seq, payload)


def response(req: Frame, payload: bytes = b"", status: int = 0) -> Frame:
    return Frame(req.cmd, req.seq, payload, status, FLAG_RESPONSE)


def error_payload(detail: int = 0, text: str = "") -> bytes:
    return struct.pack("<H", detail & 0xFFFF) + text.encode("ascii", "replace")[:64]


def parse_error_payload(payload: bytes) -> tuple[int, str]:
    if len(payload) < 2:
        return 0, ""
    (detail,) = struct.unpack_from("<H", payload)
    return detail, payload[2:66].decode("ascii", "replace")


# Parser events


@dataclass(slots=True)
class BadPayloadCrc:
    """Header was valid but the payload CRC did not match. The device answers E_BAD_CRC (section 4.2)."""

    frame: Frame
    got: int
    expected: int


@dataclass(slots=True)
class BadHeader:
    """A magic was found but the header CRC failed. Dropped silently; the parser rescans."""

    offset_bytes: bytes


@dataclass(slots=True)
class Oversize:
    """Header valid but len exceeds the parser's limit. The payload is skipped (drain)."""

    frame: Frame
    length: int


@dataclass(slots=True)
class AsciiLine:
    """A '#...' line (section 10), without the terminator."""

    text: str


@dataclass(slots=True)
class AsciiOverflow:
    """A '#' line longer than ``max_ascii`` characters (after the '#'); the device answers #ERR,UNKNOWN."""


@dataclass(slots=True)
class Garbage:
    """Bytes discarded while hunting for a magic."""

    data: bytes


ParseEvent = Frame | BadPayloadCrc | BadHeader | Oversize | AsciiLine | AsciiOverflow | Garbage


class FrameParser:
    """Incremental frame parser with the resynchronisation rules of section 3.2.

    feed() bytes in any chunking and iterate the returned events. ``max_payload`` is the largest payload the
    parser will buffer; a frame with a larger valid header produces an ``Oversize`` event and its
    ``len + 4`` bytes are skipped (resolution of a spec gap, see mcp/README.md). ``ascii_lines`` enables the
    ``#`` probe (section 10): when the parser is idle a ``#`` starts a line ended by CR or LF. Every byte up
    to the terminator belongs to the line (a 0xA5 inside a line does not start a frame). A line with more than
    ``max_ascii`` characters after the ``#`` (63 on the device, firmware ``PC_ASCII_MAX_LINE``) produces one
    ``AsciiOverflow`` event when its terminator arrives.
    """

    def __init__(self, max_payload: int = MAX_RESPONSE_PAYLOAD, ascii_lines: bool = True,
                 max_ascii: int = 255):
        self.max_payload = max_payload
        self.ascii_lines = ascii_lines
        self.max_ascii = max_ascii
        self._buf = bytearray()
        self._skip = 0  # bytes still to drain after an Oversize header
        self._ascii_discard = False  # inside an over-long ASCII line, dropping until CR/LF

    def reset(self) -> None:
        """Discard a partially received frame or line (call after INTERBYTE_TIMEOUT_S of silence)."""
        self._buf.clear()
        self._skip = 0
        self._ascii_discard = False

    @property
    def idle(self) -> bool:
        return not self._buf and not self._skip and not self._ascii_discard

    @property
    def pending_bytes(self) -> int:
        return len(self._buf)

    def feed(self, data: bytes) -> list[ParseEvent]:
        self._buf += data
        return list(self._drain())

    def _drain(self) -> Iterator[ParseEvent]:
        buf = self._buf
        while True:
            if self._skip:
                n = min(self._skip, len(buf))
                del buf[:n]
                self._skip -= n
                if self._skip:
                    return
            if not buf:
                return
            if self._ascii_discard:
                end = _find_eol(buf)
                if end < 0:
                    buf.clear()
                    return
                del buf[: end + 1]
                self._ascii_discard = False
                yield AsciiOverflow()
                continue
            # Hunt for the start of a frame or an ASCII line.
            if buf[0] != MAGIC0:
                if self.ascii_lines and buf[0] == 0x23:  # '#'
                    end = _find_eol(buf)
                    if end < 0:
                        if len(buf) - 1 > self.max_ascii:
                            buf.clear()
                            self._ascii_discard = True
                        return
                    if end - 1 > self.max_ascii:
                        del buf[: end + 1]
                        yield AsciiOverflow()
                        continue
                    line = bytes(buf[:end]).decode("ascii", "replace")
                    # the terminator ends the line; a following LF of CR LF is junk like any other byte
                    del buf[: end + 1]
                    yield AsciiLine(line)
                    continue
                i = 1
                while i < len(buf) and buf[i] != MAGIC0 and not (self.ascii_lines and buf[i] == 0x23):
                    i += 1
                junk = bytes(buf[:i])
                del buf[:i]
                # CR/LF after an ASCII line are not interesting garbage
                if junk.strip(b"\r\n"):
                    yield Garbage(junk)
                continue
            if len(buf) < 2:
                return
            if buf[1] != MAGIC1:
                yield Garbage(bytes(buf[:1]))
                del buf[:1]
                continue
            if len(buf) < HEADER_SIZE:
                return
            hdr = bytes(buf[:HEADER_SIZE])
            (crc,) = struct.unpack_from("<H", hdr, 14)
            if crc16_ccitt_false(hdr[:14]) != crc:
                # restart at the byte after the bad magic
                yield BadHeader(bytes(buf[:1]))
                del buf[:1]
                continue
            _, _, major, flags, cmd, seq, status, length = _HDR.unpack_from(hdr)
            frame = Frame(cmd, seq, b"", status, flags, major)
            if length > self.max_payload:
                del buf[:HEADER_SIZE]
                self._skip = length + CRC_SIZE
                yield Oversize(frame, length)
                continue
            total = HEADER_SIZE + length + (CRC_SIZE if length else 0)
            if len(buf) < total:
                return
            payload = bytes(buf[HEADER_SIZE: HEADER_SIZE + length])
            frame.payload = payload
            if length:
                (pcrc,) = struct.unpack_from("<I", buf, HEADER_SIZE + length)
                exp = crc32_iso_hdlc(payload)
                del buf[:total]
                if pcrc != exp:
                    yield BadPayloadCrc(frame, pcrc, exp)
                    continue
            else:
                del buf[:total]
            yield frame


def _find_eol(buf: bytearray) -> int:
    for i, b in enumerate(buf):
        if b in (0x0A, 0x0D):
            return i
    return -1


def decode_frame(data: bytes, max_payload: int = MAX_RESPONSE_PAYLOAD) -> Frame:
    """Decode exactly one frame from ``data``. Raises ProtocolError with a reason
    (``bad_magic``, ``bad_crc``, ``truncated``, ``trailing``)."""
    if len(data) < 2 or data[0] != MAGIC0 or data[1] != MAGIC1:
        raise ProtocolError("bad_magic")
    if len(data) < HEADER_SIZE:
        raise ProtocolError("truncated")
    (crc,) = struct.unpack_from("<H", data, 14)
    if crc16_ccitt_false(data[:14]) != crc:
        raise ProtocolError("bad_crc")
    _, _, major, flags, cmd, seq, status, length = _HDR.unpack_from(data)
    if length > max_payload:
        raise ProtocolError("oversize")
    total = HEADER_SIZE + length + (CRC_SIZE if length else 0)
    if len(data) < total:
        raise ProtocolError("truncated")
    if len(data) > total:
        raise ProtocolError("trailing")
    payload = bytes(data[HEADER_SIZE: HEADER_SIZE + length])
    if length:
        (pcrc,) = struct.unpack_from("<I", data, HEADER_SIZE + length)
        if pcrc != crc32_iso_hdlc(payload):
            raise ProtocolError("bad_crc")
    return Frame(cmd, seq, payload, status, flags, major)


# ---------------------------------------------------------------------------------------------------------
# Payload structs (section 8). Each class declares FIELDS = [(name, struct code)]. Codes: a single struct
# character, "Nx" (count + char, packed into a tuple), "Ns" (bytes / str) or "x"/"Nx" padding.
# unpack() tolerates longer payloads (tail ignored) and shorter ones (missing trailing fields are zero),
# per the compatibility rules of section 5.


class Payload:
    FIELDS: ClassVar[list[tuple[str, str]]] = []
    _struct: ClassVar[struct.Struct]
    _layout: ClassVar[list[tuple[str, str, int]]]  # name, kind, count

    def __init_subclass__(cls, **kw: Any) -> None:
        super().__init_subclass__(**kw)
        fmt = "<"
        layout = []
        for name, code in cls.FIELDS:
            fmt += code
            ch = code[-1]
            cnt = int(code[:-1]) if code[:-1] else 1
            if ch == "x":
                continue
            kind = "s" if ch == "s" else ("a" if cnt > 1 else "v")
            layout.append((name, kind, cnt))
        cls._struct = struct.Struct(fmt)
        cls._layout = layout

    SIZE: ClassVar[int]

    @classmethod
    def size(cls) -> int:
        return cls._struct.size

    def pack(self) -> bytes:
        vals: list[Any] = []
        for name, kind, cnt in self._layout:
            v = getattr(self, name)
            if kind == "s":
                vals.append(v.encode("ascii") if isinstance(v, str) else bytes(v))
            elif kind == "a":
                v = list(v) + [0] * (cnt - len(v))
                vals.extend(int(x) for x in v[:cnt])
            else:
                vals.append(int(v))
        return self._struct.pack(*vals)

    @classmethod
    def unpack(cls, data: bytes) -> Any:
        n = cls._struct.size
        if len(data) < n:
            data = bytes(data) + b"\0" * (n - len(data))
        raw = list(cls._struct.unpack_from(data))
        kw: dict[str, Any] = {}
        i = 0
        for name, kind, cnt in cls._layout:
            if kind == "a":
                kw[name] = tuple(raw[i: i + cnt])
                i += cnt
            else:
                kw[name] = raw[i]
                i += 1
        return cls(**kw)  # type: ignore[call-arg]

    def to_dict(self) -> dict[str, Any]:
        d = dataclasses.asdict(self)  # type: ignore[call-overload]
        for k, v in d.items():
            if isinstance(v, bytes):
                d[k] = v.rstrip(b"\0").decode("ascii", "replace")
        return d


def _cstr(b: bytes | str) -> str:
    if isinstance(b, str):
        return b
    return b.split(b"\0", 1)[0].decode("ascii", "replace")


@dataclass
class InfoResp(Payload):
    """GET_INFO response, 76 bytes (section 8.2)."""

    FIELDS = [("proto_major", "B"), ("proto_minor", "B"), ("max_request_frame", "H"),
              ("max_response_payload", "I"), ("fpga_version", "H"), ("fpga_variant", "B"), ("adc_bits", "B"),
              ("ring_words", "H"), ("channels", "B"), ("cores_per_channel", "B"), ("base_clock_hz", "I"),
              ("caps", "I"), ("gen_clock_hz", "I"), ("max_total_samples", "H"), ("min_total_samples", "H"),
              ("read_lag", "B"), ("n_ranges", "B"), ("n_hw_ranges", "B"), ("interleave_order", "B"),
              ("fw_version", "16s"), ("fw_build", "I"), ("serial", "16s"), ("screen_width", "H"),
              ("screen_height", "H")]
    proto_major: int = PROTO_MAJOR
    proto_minor: int = PROTO_MINOR
    max_request_frame: int = MAX_REQUEST_FRAME
    max_response_payload: int = MAX_RESPONSE_PAYLOAD
    fpga_version: int = 0
    fpga_variant: int = 0
    adc_bits: int = 8
    ring_words: int = 0
    channels: int = 2
    cores_per_channel: int = 2
    base_clock_hz: int = BASE_CLOCK_HZ
    caps: int = 0
    gen_clock_hz: int = 0
    max_total_samples: int = 0
    min_total_samples: int = 80
    read_lag: int = 0
    n_ranges: int = 7
    n_hw_ranges: int = 6
    interleave_order: int = 0
    fw_version: bytes = b""
    fw_build: int = 0
    serial: bytes = b""
    screen_width: int = 800
    screen_height: int = 480

    @property
    def fw_version_str(self) -> str:
        return _cstr(self.fw_version)

    @property
    def serial_str(self) -> str:
        return _cstr(self.serial)


@dataclass
class StatusResp(Payload):
    """GET_STATUS response body: 28 bytes in protocol 1.0, 32 in 1.1 (pass_ms, pass_max_ms appended; a 1.0
    device leaves them 0), 44 in 1.3 (settings_seq, capture_settings_seq, run_state, capture_origin). Also
    returned by SET_REMOTE, ARM, FORCE, STOP, WAIT and SET_ACQ_STATE."""

    FIELDS = [("control", "B"), ("acq_state", "B"), ("flags", "B"), ("ui_busy", "B"), ("capture_id", "I"),
              ("trigger_addr", "H"), ("capture_start_word", "H"), ("ms_since_arm", "I"), ("uptime_ms", "I"),
              ("watchdog_remaining_s", "H"), ("battery_level", "B"), ("exit_reason", "B"), ("reserved", "I"),
              ("pass_ms", "H"), ("pass_max_ms", "H"), ("settings_seq", "I"), ("capture_settings_seq", "I"),
              ("run_state", "B"), ("capture_origin", "B"), ("reserved2", "H")]
    control: int = 0
    acq_state: int = 0
    flags: int = 0
    ui_busy: int = 0
    capture_id: int = 0
    trigger_addr: int = 0
    capture_start_word: int = 0
    ms_since_arm: int = 0
    uptime_ms: int = 0
    watchdog_remaining_s: int = 0xFFFF
    battery_level: int = 0
    exit_reason: int = 0
    reserved: int = 0
    #: 1.1: interval between the last two main-loop polls, and its maximum since the previous GET_STATUS
    pass_ms: int = 0
    pass_max_ms: int = 0
    #: 1.3: +1 on every change of a capture-relevant setting (UI or host); wraps at 2^32
    settings_seq: int = 0
    #: 1.3: settings_seq when the capture ``capture_id`` was started
    capture_settings_seq: int = 0
    #: 1.3: RunState
    run_state: int = 0
    #: 1.3: CaptureOrigin of ``capture_id``
    capture_origin: int = 0
    reserved2: int = 0

    @property
    def shared(self) -> bool:
        return self.control == Control.SHARED


@dataclass
class SetAcqStateReq(Payload):
    """SET_ACQ_STATE request (protocol 1.3): u8 state (RunState.STOP or RunState.RUN), 3 reserved."""

    FIELDS = [("state", "B"), ("_r", "3x")]
    state: int = 0


@dataclass
class CaptureInfoHeader(Payload):
    """GET_CAPTURE_INFO response, first 36 bytes (protocol 1.3): the settings the last DONE capture (any
    origin) was taken with. Followed by one CaptureChannelInfo (16 bytes) per channel, CH1 first."""

    FIELDS = [("capture_id", "I"), ("settings_seq", "I"), ("origin", "B"), ("flags", "B"), ("trigger_addr", "H"),
              ("capture_start_word", "H"), ("_r0", "2x"), ("divider", "I"), ("timeperdiv_index", "B"),
              ("trig_source", "B"), ("trig_edge", "B"), ("trig_level", "B"), ("trig_mode", "B"), ("_r1", "3x"),
              ("pretrigger_samples", "I"), ("total_samples", "I")]
    capture_id: int = 0
    settings_seq: int = 0
    origin: int = 0
    flags: int = 0
    trigger_addr: int = 0
    capture_start_word: int = 0
    divider: int = 0
    timeperdiv_index: int = 0xFF
    trig_source: int = 0
    trig_edge: int = 0
    trig_level: int = 128
    trig_mode: int = 0
    pretrigger_samples: int = 0
    total_samples: int = 0

    F_TRIGGERED: ClassVar[int] = 0x01
    F_AUTO_TIMEOUT: ClassVar[int] = 0x02
    F_OVERLOAD_CH1: ClassVar[int] = 0x04
    F_OVERLOAD_CH2: ClassVar[int] = 0x08
    #: a setting changed while the capture ran (settings_seq is then SEQ_INVALID)
    F_SETTINGS_CHANGED: ClassVar[int] = 0x10


@dataclass
class CaptureChannelInfo(Payload):
    FIELDS = [("enable", "B"), ("range", "B"), ("coupling", "B"), ("invert", "B"), ("probe_index", "B"),
              ("_r", "x"), ("offset_pwm", "H"), ("uv_per_lsb_q8", "I"), ("zero_code_q8", "i")]
    enable: int = 1
    range: int = 5
    coupling: int = 0
    invert: int = 0
    probe_index: int = 1
    offset_pwm: int = 0
    uv_per_lsb_q8: int = 0
    zero_code_q8: int = 128 * 256


@dataclass
class CaptureInfo:
    """Decoded GET_CAPTURE_INFO (68 bytes)."""

    header: CaptureInfoHeader
    channels: list[CaptureChannelInfo]

    SIZE: ClassVar[int] = 36 + 2 * 16

    def pack(self) -> bytes:
        return self.header.pack() + b"".join(c.pack() for c in self.channels)

    @classmethod
    def unpack(cls, data: bytes) -> "CaptureInfo":
        h = CaptureInfoHeader.unpack(data)
        chans = [CaptureChannelInfo.unpack(data[36 + 16 * i: 52 + 16 * i]) for i in range(2)]
        return cls(h, chans)

    def channel_config(self, ch: int) -> "ChannelConfig":
        """The capture-time channel settings as a CHANNEL_CONFIG (dcoffset / center_cal are not carried)."""
        c = self.channels[ch]
        return ChannelConfig(channel=ch, enable=c.enable, range=c.range, hw_range=min(c.range, 5),
                             coupling=c.coupling, invert=c.invert, probe_index=c.probe_index, offset_pwm=c.offset_pwm,
                             uv_per_lsb_q8=c.uv_per_lsb_q8, zero_code_q8=c.zero_code_q8)

    def trigger(self) -> "Trigger":
        h = self.header
        return Trigger(source=h.trig_source, edge=h.trig_edge, level=h.trig_level, mode=h.trig_mode,
                       pretrigger_samples=h.pretrigger_samples, total_samples=h.total_samples,
                       pretrigger_words=h.pretrigger_samples // 2, total_words=h.total_samples // 2)

    def timebase(self) -> "Timebase":
        return timebase_for_divider(self.header.divider, self.header.timeperdiv_index)

    def settings_key(self) -> dict[str, Any]:
        """Capture-relevant settings as a flat dict (for reporting what changed)."""
        h = self.header
        d: dict[str, Any] = {"divider": h.divider, "trig_source": h.trig_source, "trig_edge": h.trig_edge,
                             "trig_level": h.trig_level, "trig_mode": h.trig_mode,
                             "pretrigger_samples": h.pretrigger_samples, "total_samples": h.total_samples}
        for i, c in enumerate(self.channels):
            for k in ("enable", "range", "coupling", "invert", "probe_index", "offset_pwm"):
                d[f"CH{i + 1}.{k}"] = getattr(c, k)
        return d


# SD sector commands (protocol 1.3, fw 0.2.2, section 14 row 40)

SD_SECTOR = 512
SD_STAGE_BYTES = 65536
SD_STAGE_MAX_CHUNK = 484
SD_MAX_SECTORS = 128
SD_WRITE_MAGIC = 0x52574453  # 'SDWR'
SD_FLAG_VERIFY = 0x01
SD_WR_CONFIG = 0x01  # allow sectors 708-710
SD_WR_BOOT = 0x02  # allow sectors 16-79
SD_EGON_NONE, SD_EGON_BT0, SD_EGON_EXE = 0, 1, 2
SD_CHECK_NOT, SD_CHECK_OK, SD_CHECK_BAD, SD_CHECK_LENGTH = 0, 1, 2, 3


@dataclass
class SdHeaderInfo(Payload):
    FIELDS = [("sector", "I"), ("magic", "B"), ("check", "B"), ("_r", "2x"), ("length", "I"), ("checksum", "I")]
    sector: int = 0
    magic: int = 0
    check: int = 0
    length: int = 0
    checksum: int = 0


@dataclass
class SdInfo:
    """SD_INFO response (64 bytes): card, partition, write window, eGON headers at sectors 16, 80 and 750."""

    card_sectors: int
    part_start: int
    write_end: int
    stage_bytes: int
    headers: list[SdHeaderInfo]

    def pack(self) -> bytes:
        return struct.pack("<IIII", self.card_sectors, self.part_start, self.write_end, self.stage_bytes) + \
            b"".join(h.pack() for h in self.headers)

    @classmethod
    def unpack(cls, data: bytes) -> "SdInfo":
        data = bytes(data) + b"\0" * max(0, 64 - len(data))
        a, b, c, d = struct.unpack_from("<IIII", data)
        return cls(a, b, c, d, [SdHeaderInfo.unpack(data[16 + 16 * i: 32 + 16 * i]) for i in range(3)])

    def header(self, sector: int) -> SdHeaderInfo | None:
        return next((h for h in self.headers if h.sector == sector), None)


@dataclass
class SdReadReq(Payload):
    FIELDS = [("sector", "I"), ("count", "H"), ("_r", "2x")]
    sector: int = 0
    count: int = 1


@dataclass
class SdReadHeader(Payload):
    FIELDS = [("sector", "I"), ("count", "H"), ("_r", "2x"), ("crc32", "I"), ("_r2", "4x")]
    sector: int = 0
    count: int = 0
    crc32: int = 0


@dataclass
class SdStageHeader(Payload):
    """SD_STAGE request head (then ``length`` data bytes) and response (12 bytes: ..., u32 crc32)."""

    FIELDS = [("offset", "I"), ("length", "H"), ("_r", "2x")]
    offset: int = 0
    length: int = 0


@dataclass
class SdStageResp(Payload):
    FIELDS = [("offset", "I"), ("length", "H"), ("_r", "2x"), ("crc32", "I")]
    offset: int = 0
    length: int = 0
    crc32: int = 0


@dataclass
class SdWriteReq(Payload):
    FIELDS = [("sector", "I"), ("count", "H"), ("flags", "B"), ("_r", "x"), ("crc32", "I"), ("magic", "I")]
    sector: int = 0
    count: int = 1
    flags: int = 0
    crc32: int = 0
    magic: int = SD_WRITE_MAGIC


@dataclass
class SdWriteResp(Payload):
    FIELDS = [("sector", "I"), ("count", "H"), ("_r", "2x"), ("crc32", "I"), ("_r2", "4x")]
    sector: int = 0
    count: int = 0
    crc32: int = 0


EGON_CHECKSUM_STAMP = 0x5F0A6C39


def egon_info(buf: bytes) -> tuple[int, int, int]:
    """(magic SD_EGON_*, header length, stored checksum) of an eGON image head."""
    if len(buf) < 20:
        return SD_EGON_NONE, 0, 0
    tag = bytes(buf[4:12])
    magic = SD_EGON_BT0 if tag == b"eGON.BT0" else SD_EGON_EXE if tag == b"eGON.EXE" else SD_EGON_NONE
    stored, length = struct.unpack_from("<II", buf, 12)
    return magic, length, stored


def egon_checksum(buf: bytes, length: int) -> int:
    """eGON checksum (firmware/tools/mksunxi.py): stamp in the field, u32 LE word sum of ``length`` bytes."""
    tmp = bytearray(buf[:length])
    struct.pack_into("<I", tmp, 12, EGON_CHECKSUM_STAMP)
    return sum(struct.unpack_from(f"<{length // 4}I", tmp)) & 0xFFFFFFFF


@dataclass
class SetRemoteReq(Payload):
    FIELDS = [("action", "B"), ("flags", "B"), ("watchdog_s", "H")]
    action: int = 1
    flags: int = 0
    watchdog_s: int = 0

    KEEP_SETTINGS: ClassVar[int] = 0x01
    SHOW_TRACES: ClassVar[int] = 0x02
    IGNORE_DTR: ClassVar[int] = 0x04


@dataclass
class BatteryResp(Payload):
    FIELDS = [("level", "B"), ("charging", "B"), ("keyadc_raw", "B"), ("reserved", "B")]
    level: int = 0
    charging: int = 0
    keyadc_raw: int = 0
    reserved: int = 0


@dataclass
class TestPatternReq(Payload):
    FIELDS = [("length", "I"), ("pattern", "B"), ("_r", "3x")]
    length: int = 0
    pattern: int = 0


def test_pattern_bytes(length: int, pattern: int) -> bytes:
    """Bytes the device sends for TEST_PATTERN (section 8.2)."""
    if pattern == 0:
        return bytes(i & 0xFF for i in range(length))
    if pattern == 1:
        return b"\0" * length
    if pattern == 2:
        return b"\xff" * length
    if pattern == 3:
        # xorshift32 (protocol 14.12): state = length; before bytes 4k..4k+3 the state steps once
        # (x ^= x << 13; x ^= x >> 17; x ^= x << 5); the 4 bytes are the new state little-endian.
        x = length & 0xFFFFFFFF
        out = bytearray()
        while len(out) < length:
            x ^= (x << 13) & 0xFFFFFFFF
            x ^= x >> 17
            x ^= (x << 5) & 0xFFFFFFFF
            out += x.to_bytes(4, "little")
        return bytes(out[:length])
    raise ValueError(pattern)


@dataclass
class ChannelReq(Payload):
    """Request of GET_CHANNEL and GET_CAL: one byte channel."""

    FIELDS = [("channel", "B")]
    channel: int = 0


@dataclass
class ChannelConfig(Payload):
    """CHANNEL_CONFIG, 24 bytes (section 8.3)."""

    FIELDS = [("channel", "B"), ("enable", "B"), ("range", "B"), ("hw_range", "B"), ("coupling", "B"),
              ("invert", "B"), ("probe_index", "B"), ("_r0", "x"), ("dcoffset", "h"), ("offset_pwm", "H"),
              ("center_cal", "H"), ("_r1", "2x"), ("uv_per_lsb_q8", "I"), ("zero_code_q8", "i")]
    channel: int = 0
    enable: int = 1
    range: int = 5
    hw_range: int = 5
    coupling: int = 0
    invert: int = 0
    probe_index: int = 1
    dcoffset: int = 0
    offset_pwm: int = 0
    center_cal: int = 0
    uv_per_lsb_q8: int = 0
    zero_code_q8: int = 128 * 256

    @property
    def volts_per_lsb(self) -> float:
        """Volts per code at the BNC (1x)."""
        return self.uv_per_lsb_q8 / 256.0 * 1e-6

    @property
    def zero_code(self) -> float:
        return self.zero_code_q8 / 256.0

    @property
    def probe_factor(self) -> float:
        return PROBE_FACTORS[self.probe_index] if self.probe_index < len(PROBE_FACTORS) else 1.0


@dataclass
class SetChannelReq(Payload):
    FIELDS = [("channel", "B"), ("mask", "B"), ("enable", "B"), ("range", "B"), ("coupling", "B"),
              ("invert", "B"), ("probe_index", "B"), ("offset_mode", "B"), ("offset", "i")]
    channel: int = 0
    mask: int = 0
    enable: int = 0
    range: int = 0
    coupling: int = 0
    invert: int = 0
    probe_index: int = 0
    offset_mode: int = 0
    offset: int = 0

    M_ENABLE: ClassVar[int] = 0x01
    M_RANGE: ClassVar[int] = 0x02
    M_COUPLING: ClassVar[int] = 0x04
    M_OFFSET: ClassVar[int] = 0x08
    M_INVERT: ClassVar[int] = 0x10
    M_PROBE: ClassVar[int] = 0x20


@dataclass
class Timebase(Payload):
    FIELDS = [("timeperdiv_index", "B"), ("samplerate_index", "B"), ("_r", "2x"), ("divider", "I"),
              ("sample_rate_hz", "I"), ("sample_period_ps", "I")]
    timeperdiv_index: int = 0xFF
    samplerate_index: int = 0xFF
    divider: int = 0
    sample_rate_hz: int = 0
    sample_period_ps: int = 0

    @property
    def dt(self) -> float:
        return (self.divider + 1) / BASE_CLOCK_HZ


def timebase_for_divider(n: int, tpd_index: int = 0xFF, sr_index: int = 0xFF) -> Timebase:
    return Timebase(tpd_index, sr_index, n, BASE_CLOCK_HZ // (n + 1), min(5000 * (n + 1), 0xFFFFFFFF))


@dataclass
class SetTimebaseReq(Payload):
    FIELDS = [("mode", "B"), ("_r", "3x"), ("value", "I")]
    mode: int = 1
    value: int = 0


@dataclass
class Trigger(Payload):
    FIELDS = [("source", "B"), ("edge", "B"), ("level", "B"), ("mode", "B"), ("pretrigger_samples", "I"),
              ("total_samples", "I"), ("holdoff", "I"), ("auto_timeout", "I"), ("pretrigger_words", "H"),
              ("total_words", "H"), ("reserved", "I")]
    source: int = 0
    edge: int = 0
    level: int = 128
    mode: int = 0
    pretrigger_samples: int = 0
    total_samples: int = 0
    holdoff: int = 0
    auto_timeout: int = 0
    pretrigger_words: int = 0
    total_words: int = 0
    reserved: int = 0


TRIG_MODES = ("auto", "normal", "single")
EDGES = ("rising", "falling")


@dataclass
class SetTriggerReq(Payload):
    FIELDS = [("mask", "H"), ("source", "B"), ("edge", "B"), ("level", "B"), ("mode", "B"), ("_r", "2x"),
              ("pretrigger_samples", "I"), ("total_samples", "I"), ("holdoff", "I"), ("auto_timeout", "I")]
    mask: int = 0
    source: int = 0
    edge: int = 0
    level: int = 0
    mode: int = 0
    pretrigger_samples: int = 0
    total_samples: int = 0
    holdoff: int = 0
    auto_timeout: int = 0

    M_SOURCE: ClassVar[int] = 1 << 0
    M_EDGE: ClassVar[int] = 1 << 1
    M_LEVEL: ClassVar[int] = 1 << 2
    M_MODE: ClassVar[int] = 1 << 3
    M_PRETRIGGER: ClassVar[int] = 1 << 4
    M_TOTAL: ClassVar[int] = 1 << 5
    M_HOLDOFF: ClassVar[int] = 1 << 6
    M_AUTO_TIMEOUT: ClassVar[int] = 1 << 7


@dataclass
class U32Req(Payload):
    """ARM (reserved), WAIT (timeout_ms), ENTER_FEL (magic)."""

    FIELDS = [("value", "I")]
    value: int = 0


@dataclass
class ReadRawReq(Payload):
    FIELDS = [("core_mask", "B"), ("addr_mode", "B"), ("layout", "B"), ("flags", "B"), ("address", "i"),
              ("count", "I"), ("reserved", "I")]
    core_mask: int = 0x0F
    addr_mode: int = 1
    layout: int = 0
    flags: int = 0
    address: int = 0
    count: int = 0
    reserved: int = 0

    #: protocol <= 1.2: read while ARMED; 1.3 ("ANY"): read the ring as it is, whatever it holds
    ALLOW_NOT_DONE: ClassVar[int] = 0x01
    ADDR_ABSOLUTE: ClassVar[int] = 0
    ADDR_CAPTURE: ClassVar[int] = 1
    ADDR_TRIGGER: ClassVar[int] = 2


#: core_mask bits in ascending order: CH1 A, CH1 B, CH2 A, CH2 B.
CORE_NAMES = ("CH1A", "CH1B", "CH2A", "CH2B")


@dataclass
class ReadRawHeader(Payload):
    FIELDS = [("capture_id", "I"), ("start_word", "H"), ("ring_words", "H"), ("count", "I"),
              ("core_mask", "B"), ("layout", "B"), ("acq_state", "B"), ("read_lag", "B")]
    capture_id: int = 0
    start_word: int = 0
    ring_words: int = 0
    count: int = 0
    core_mask: int = 0
    layout: int = 0
    acq_state: int = 0
    read_lag: int = 0


@dataclass
class ReadTraceReq(Payload):
    FIELDS = [("ch_mask", "B"), ("flags", "B"), ("_r", "2x"), ("start", "I"), ("count", "I")]
    ch_mask: int = 3
    flags: int = 0
    start: int = 0
    count: int = 0

    REREAD: ClassVar[int] = 0x01
    NO_INVERT: ClassVar[int] = 0x02


@dataclass
class ReadTraceHeader(Payload):
    FIELDS = [("capture_id", "I"), ("sample_count", "I"), ("start", "I"), ("count", "I"),
              ("hw_trigger_index", "i"), ("sw_trigger_index", "i"), ("divider", "I"), ("ch_mask", "B"),
              ("origin", "B"), ("_r", "2x")]
    capture_id: int = 0
    sample_count: int = 0
    start: int = 0
    count: int = 0
    hw_trigger_index: int = 0
    sw_trigger_index: int = -1
    divider: int = 0
    ch_mask: int = 0
    origin: int = 0


@dataclass
class TraceChannelMeta(Payload):
    FIELDS = [("channel", "B"), ("range", "B"), ("coupling", "B"), ("flags", "B"), ("uv_per_lsb_q8", "I"),
              ("zero_code_q8", "i"), ("adc_a_comp", "h"), ("adc_b_comp", "h")]
    channel: int = 0
    range: int = 0
    coupling: int = 0
    flags: int = 0
    uv_per_lsb_q8: int = 0
    zero_code_q8: int = 0
    adc_a_comp: int = 0
    adc_b_comp: int = 0


@dataclass
class ReadLiveReq(Payload):
    FIELDS = [("core", "B"), ("_r", "x"), ("count", "H")]
    core: int = 0
    count: int = 1


@dataclass
class AcquireAvgReq(Payload):
    FIELDS = [("n", "H"), ("ch_mask", "B"), ("flags", "B"), ("per_capture_timeout_ms", "I"), ("start", "I"),
              ("count", "I"), ("reserved", "I")]
    n: int = 1
    ch_mask: int = 1
    flags: int = 0
    per_capture_timeout_ms: int = 1000
    start: int = 0
    count: int = 0
    reserved: int = 0

    REQUIRE_TRIGGER: ClassVar[int] = 0x01


@dataclass
class AcquireAvgHeader(Payload):
    FIELDS = [("n_used", "H"), ("n_dropped", "H"), ("first_capture_id", "I"), ("count", "I"),
              ("ch_mask", "B"), ("_r", "3x")]
    n_used: int = 0
    n_dropped: int = 0
    first_capture_id: int = 0
    count: int = 0
    ch_mask: int = 0


@dataclass
class Gen(Payload):
    FIELDS = [("enable", "B"), ("_r", "3x"), ("phase_inc", "I"), ("duty_threshold", "I"),
              ("freq_mhz_actual", "Q"), ("duty_bp_actual", "H"), ("_r2", "2x")]
    enable: int = 0
    phase_inc: int = 0
    duty_threshold: int = 0
    freq_mhz_actual: int = 0
    duty_bp_actual: int = 0

    @property
    def frequency_hz(self) -> float:
        return self.freq_mhz_actual / 1000.0

    @property
    def duty(self) -> float:
        return self.duty_bp_actual / 10000.0


@dataclass
class SetGenReq(Payload):
    FIELDS = [("mask", "B"), ("enable", "B"), ("freq_mode", "B"), ("duty_mode", "B"), ("freq", "Q"),
              ("duty", "I"), ("reserved", "I")]
    mask: int = 0
    enable: int = 0
    freq_mode: int = 0
    duty_mode: int = 0
    freq: int = 0
    duty: int = 0
    reserved: int = 0

    M_ENABLE: ClassVar[int] = 1
    M_FREQ: ClassVar[int] = 2
    M_DUTY: ClassVar[int] = 4


def gen_phase_inc(freq_mhz: int, gen_clock_hz: int = GEN_CLOCK_HZ) -> int:
    """phase_inc = round(freq_mHz x 2^32 / (gen_clock x 1000)), integer arithmetic (section 8.6)."""
    den = gen_clock_hz * 1000
    return (freq_mhz * (1 << 32) + den // 2) // den


def gen_threshold(duty_bp: int) -> int:
    return (duty_bp * 0xFFFFFFFF + 5000) // 10000


def gen_freq_mhz(phase_inc: int, gen_clock_hz: int = GEN_CLOCK_HZ) -> int:
    return (phase_inc * gen_clock_hz * 1000 + (1 << 31)) >> 32


def gen_duty_bp(threshold: int) -> int:
    return (threshold * 10000 + 0x7FFFFFFF) // 0xFFFFFFFF


@dataclass
class CalResp(Payload):
    FIELDS = [("channel", "B"), ("_r0", "3x"), ("dc_calibration_offset", "7H"), ("_r1", "2x"),
              ("input_calibration", "7i"), ("adc_a_comp", "h"), ("adc_b_comp", "h"), ("dc_shift_center", "I"),
              ("dc_shift_size", "i"), ("dc_shift_value", "i"), ("adc_a_rawavg", "I"), ("adc_b_rawavg", "I")]
    channel: int = 0
    dc_calibration_offset: tuple[int, ...] = field(default_factory=lambda: (0,) * 7)
    input_calibration: tuple[int, ...] = field(default_factory=lambda: (0,) * 7)
    adc_a_comp: int = 0
    adc_b_comp: int = 0
    dc_shift_center: int = 0
    dc_shift_size: int = 0
    dc_shift_value: int = 0
    adc_a_rawavg: int = 0
    adc_b_rawavg: int = 0


@dataclass
class ReadScreenReq(Payload):
    FIELDS = [("row", "H"), ("nrows", "H")]
    row: int = 0
    nrows: int = 40


@dataclass
class ReadScreenHeader(Payload):
    FIELDS = [("width", "H"), ("row", "H"), ("nrows", "H"), ("format", "H")]
    width: int = 800
    row: int = 0
    nrows: int = 0
    format: int = 1


#: GET_LINK_STATS counter names in wire order (protocol 1.2, docs/protocol.md section 14 item 28).
LINK_STATS_NAMES = (
    "rx_packets", "rx_bytes", "rx_stalls", "rx_swept", "tx_packets", "tx_zlps", "tx_kicked", "tx_flushes",
    "link_resets", "dtr_up", "dtr_down", "frames", "hdr_crc_errors", "payload_crc_errors", "oversize",
    "version_errors", "responses_dropped", "rx_timeouts", "junk_bytes", "responses", "duplicates", "tx_aborts",
    "disp_main", "disp_busy", "disp_isr", "held", "isr_skipped",
    # fw 0.2.0: ms from init to the first main-loop poll, duration of the first main-loop pass
    "first_poll_ms", "first_pass_ms",
    # fw 0.2.2: ms from the first main-loop poll to the first capture arm / DONE (| 1; 0 = not yet)
    "first_arm_ms", "first_done_ms",
)


def pack_link_stats(counters: dict[str, int]) -> bytes:
    vals = [int(counters.get(n, 0)) & 0xFFFFFFFF for n in LINK_STATS_NAMES]
    return struct.pack(f"<HH{len(vals)}I", len(vals), 0, *vals)


def parse_link_stats(payload: bytes) -> dict[str, int]:
    """u16 n, u16 0, n x u32. Unknown extra counters are named ``c<index>``."""
    if len(payload) < 4:
        return {}
    (n,) = struct.unpack_from("<H", payload)
    n = min(n, (len(payload) - 4) // 4)
    vals = struct.unpack_from(f"<{n}I", payload, 4)
    return {(LINK_STATS_NAMES[i] if i < len(LINK_STATS_NAMES) else f"c{i}"): v for i, v in enumerate(vals)}


#: Request sizes of v1.0 for fixed-size requests. Shorter requests get E_BAD_LEN, longer ones are accepted
#: and the tail ignored (resolution in mcp/README.md "Spec ambiguities").
REQUEST_SIZES: dict[Cmd, int] = {
    Cmd.GET_INFO: 0, Cmd.GET_STATUS: 0, Cmd.GET_LINK_STATS: 0, Cmd.SET_REMOTE: SetRemoteReq.size(), Cmd.GET_BATTERY: 0,
    Cmd.TEST_PATTERN: TestPatternReq.size(), Cmd.GET_CHANNEL: 1, Cmd.SET_CHANNEL: SetChannelReq.size(),
    Cmd.GET_TIMEBASE: 0, Cmd.SET_TIMEBASE: SetTimebaseReq.size(), Cmd.GET_TRIGGER: 0,
    Cmd.SET_TRIGGER: SetTriggerReq.size(), Cmd.ARM: 4, Cmd.FORCE: 0, Cmd.STOP: 0, Cmd.WAIT: 4,
    Cmd.READ_RAW: ReadRawReq.size(), Cmd.READ_TRACE: ReadTraceReq.size(), Cmd.READ_LIVE: ReadLiveReq.size(),
    Cmd.ACQUIRE_AVG: AcquireAvgReq.size(), Cmd.GET_GEN: 0, Cmd.SET_GEN: SetGenReq.size(), Cmd.GET_CAL: 1,
    Cmd.READ_SCREEN: ReadScreenReq.size(), Cmd.ENTER_FEL: 4, Cmd.SET_ACQ_STATE: SetAcqStateReq.size(),
    Cmd.GET_CAPTURE_INFO: 0, Cmd.SD_INFO: 4, Cmd.SD_READ_SECTORS: 8, Cmd.SD_STAGE: 8, Cmd.SD_WRITE_SECTORS: 16,
}


# DEBUG_FPGA operation list (section 8.8)

DBG_WRITE_CMD = 1
DBG_WRITE_DATA = 2
DBG_READ_DATA = 3


def encode_debug_ops(ops: list[tuple[int, bytes | int]]) -> bytes:
    """ops: (DBG_WRITE_CMD, cmd_byte) | (DBG_WRITE_DATA, bytes) | (DBG_READ_DATA, n)."""
    out = bytearray()
    for op, arg in ops:
        if op == DBG_WRITE_CMD:
            out += bytes((op, 1, int(arg) & 0xFF))  # type: ignore[arg-type]
        elif op == DBG_WRITE_DATA:
            data = bytes(arg)  # type: ignore[arg-type]
            if not 1 <= len(data) <= 255:
                raise ValueError("WRITE_DATA takes 1..255 bytes")
            out += bytes((op, len(data))) + data
        elif op == DBG_READ_DATA:
            n = int(arg)  # type: ignore[arg-type]
            if not 1 <= n <= 256:
                raise ValueError("READ_DATA takes 1..256 bytes")
            out += bytes((op, n & 0xFF))
        else:
            raise ValueError(f"unknown debug op {op}")
    return bytes(out)


def decode_debug_ops(data: bytes) -> list[tuple[int, bytes | int]]:
    ops: list[tuple[int, bytes | int]] = []
    i = 0
    while i < len(data):
        if i + 2 > len(data):
            raise ProtocolError("truncated debug op")
        op, n = data[i], data[i + 1]
        i += 2
        if op == DBG_WRITE_CMD:
            if n != 1 or i + 1 > len(data):
                raise ProtocolError("WRITE_CMD needs n = 1")
            ops.append((op, data[i]))
            i += 1
        elif op == DBG_WRITE_DATA:
            if i + n > len(data) or n == 0:
                raise ProtocolError("WRITE_DATA truncated")
            ops.append((op, bytes(data[i: i + n])))
            i += n
        elif op == DBG_READ_DATA:
            ops.append((op, 256 if n == 0 else n))
        else:
            raise ProtocolError(f"unknown debug op {op}")
    return ops


# ---------------------------------------------------------------------------------------------------------
# Scaling (section 8.5) [U: bench test T8]. Shared by simulator and host so they agree; the firmware is
# expected to compute uv_per_lsb_q8 / zero_code_q8 with the same integer formulas.

#: volt_calc_data[1x][range].mul_factor (S100 variables.c:905).
MUL_FACTOR_1X = (10000, 5000, 2000, 1000, 400, 200, 100)
#: signal_adjusters[] default input_calibration (S100 variables.c:920).
DEFAULT_INPUT_CALIBRATION = (3600000, 3600000, 3700000, 3700000, 3800000, 3800000, 7600000)
VOLTAGE_SHIFTER = 21


def uv_per_lsb_q8(input_calibration: int, rng: int) -> int:
    """uv_per_lsb_q8 = round(256 x input_calibration / 2^21 x mul_factor_1x[range] x 10). [U] T8."""
    num = input_calibration * MUL_FACTOR_1X[rng] * 10 * 256
    return (num + (1 << (VOLTAGE_SHIFTER - 1))) >> VOLTAGE_SHIFTER


def zero_code_q8(dcoffset: int, dc_shift_value: int) -> int:
    """zero_code = 128 - dcoffset x 100 / dc_shift_value, x 256. [U] formula (section 8.5, T8)."""
    if dc_shift_value == 0:
        return 128 * 256
    return 128 * 256 - (dcoffset * 100 * 256) // dc_shift_value


def hexdump(b: bytes) -> str:
    return b.hex(" ")
