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
#: mode: SHARED_MODE cap, GET_STATUS settings/run fields, SET_ACQ_STATE, GET_CAPTURE_INFO; 1.4 (fw 0.2.9): EXT
#: trigger (source 2 on 0x1533+ bitstreams), GET_STATUS ext_flags, GET_CAPTURE_INFO flag bit 5; 1.5 (fw 0.2.12):
#: GET_EVENT_LOG, link counter 47 usb_irq_polled; 1.6 (fw 0.2.27): FFT_BENCH; 1.7 (fw 0.2.28): spectrum view,
#: READ_SPECTRUM, GET_STATUS display_mode; 1.9 (fw 0.2.30, FPGA 0x1534): decimation filter (GET_DECIM / SET_DECIM,
#: caps DECIM, 16-bit samples: READ_TRACE WIDE, READ_RAW layout 2, GET_CAPTURE_INFO 76 bytes); 1.8 was not used;
#: 1.10 (fw 0.2.43): calibration data v2, GET_CAL 248 bytes; 1.11 (fw 0.2.44, FPGA 0x1535): CIC compensation FIR
#: (caps FIR, SET_DECIM flags bit 3, 0x29 bits 6 / 5); 1.12 (fw 0.2.45): calibration data v3 (per-core, per-class
#: gains), GET_CAL 256 bytes (v2 blocks are no longer used); 1.13 (fw 0.2.46): phosphor view, display_mode 4; 1.14
#: (fw 0.2.49): USB suspend watch, link counters 60-61, event type 20
PROTO_MINOR = 16
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
    GET_BAD_FRAME = 0x0008  # protocol 1.2, fw 0.1.5
    GET_EVENT_LOG = 0x0009  # protocol 1.5, fw 0.2.12: last 64 USB/link events
    GET_HEALTH = 0x000A  # protocol 1.16, fw 0.2.52: USB state now, CP15, unit life, the stored restart record
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
    READ_SPECTRUM = 0x020A  # protocol 1.7, fw 0.2.28: the spectrum the scope's spectrum view shows (main loop)
    GET_DECIM = 0x020C  # protocol 1.9, fw 0.2.30, cap DECIM: decimation filter setting and state
    SET_DECIM = 0x020D  # protocol 1.9: the decimation filter (the UI's acquisition Filter setting)
    GET_GEN = 0x0300
    SET_GEN = 0x0301
    GET_CAL = 0x0400
    READ_SCREEN = 0x0401
    SD_INFO = 0x0500  # protocol 1.3, fw 0.2.2, cap SD_SECTORS
    SD_READ_SECTORS = 0x0501
    SD_STAGE = 0x0502
    SD_WRITE_SECTORS = 0x0503
    SAVE_REPORT = 0x0504  # protocol 1.16, fw 0.2.52, cap SD_SECTORS: write reports/report-PPPPP-NN.txt on the card
    DEBUG_FPGA = 0x0F00
    ENTER_FEL = 0x0F01
    FFT_BENCH = 0x0F02  # protocol 1.6, fw 0.2.27: time the on-scope FFT (main loop, any bitstream)


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
BUSY_ALLOWED = frozenset({Cmd.PING, Cmd.GET_INFO, Cmd.GET_STATUS, Cmd.GET_BATTERY, Cmd.GET_LINK_STATS,
                          Cmd.GET_EVENT_LOG, Cmd.GET_HEALTH})
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
    #: protocol 1.9 (FPGA 0x1534+): decimation filter with 16-bit samples
    DECIM = 1 << 15
    #: protocol 1.11 (FPGA 0x1535+): CIC compensation FIR after the decimation filter
    FIR = 1 << 16


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
    Cmd.SAVE_REPORT: Caps.SD_SECTORS,
    Cmd.GET_DECIM: Caps.DECIM,
    Cmd.SET_DECIM: Caps.DECIM,
}


class FpgaVariant(enum.IntEnum):
    UNKNOWN = 0
    STOCK = 1
    AL3 = 2
    EF2 = 3
    EP4 = 4


#: 0x1533: AL3 with the external trigger on the calibration pin (fpga/al3, fpga-gen2; caps EXT_TRIGGER)
FPGA_VERSION_WORDS = {0x1432: FpgaVariant.STOCK, 0x1532: FpgaVariant.AL3, 0x1533: FpgaVariant.AL3,
                      0x1632: FpgaVariant.EF2,
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


class ExtFlags(enum.IntFlag):
    """GET_STATUS ext_flags (protocol 1.4, byte 42), refreshed at every main-loop poll from FPGA cmd 0x28."""

    LEVEL = 1 << 0  # live level of the EXT pin
    GENERATOR = 1 << 1  # the generator drives the pin
    EDGE_SEEN = 1 << 2  # an EXT edge since the last sampling reset (every arm clears it)


class DisplayMode(enum.IntEnum):
    """GET_STATUS display_mode (protocol 1.7, byte 43; 0 before): what the scope's trace area shows."""

    SCOPE = 0
    SPECTRUM = 1
    #: fw 0.2.32: the spectrum view's waterfall and density sub-views (READ_SPECTRUM as in SPECTRUM)
    WATERFALL = 2
    DENSITY = 3
    #: 1.13 (fw 0.2.46): the digital phosphor view (traces graded from per-pixel hit counts; data unchanged)
    PHOSPHOR = 4


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
              ("run_state", "B"), ("capture_origin", "B"), ("ext_flags", "B"), ("display_mode", "B")]
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
    #: 1.4: ExtFlags (0 unless caps EXT_TRIGGER)
    ext_flags: int = 0
    #: 1.7: DisplayMode (0 before 1.7)
    display_mode: int = 0

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
    #: 1.4: an EXT edge was seen during this capture
    F_EXT_EDGE: ClassVar[int] = 0x20


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
    """Decoded GET_CAPTURE_INFO (68 bytes; 76 from protocol 1.9 with the capture's decimation filter)."""

    header: CaptureInfoHeader
    channels: list[CaptureChannelInfo]
    #: 1.9: the capture's mode (FPGA 0x29 byte 0 layout, 0 = legacy), S, EXT latency shift (samples), N
    decim_mode: int = 0
    decim_shift: int = 0
    ext_shift: int = 0
    decim_n: int = 0
    has_decim: bool = False

    SIZE: ClassVar[int] = 36 + 2 * 16

    def pack(self) -> bytes:
        out = self.header.pack() + b"".join(c.pack() for c in self.channels)
        if self.has_decim:
            out += struct.pack("<BBHI", self.decim_mode, self.decim_shift, self.ext_shift, self.decim_n)
        return out

    @classmethod
    def unpack(cls, data: bytes) -> "CaptureInfo":
        h = CaptureInfoHeader.unpack(data)
        chans = [CaptureChannelInfo.unpack(data[36 + 16 * i: 52 + 16 * i]) for i in range(2)]
        ci = cls(h, chans)
        if len(data) >= 76:
            ci.decim_mode, ci.decim_shift, ci.ext_shift, ci.decim_n = struct.unpack_from("<BBHI", data, 68)
            ci.has_decim = True
        return ci

    @property
    def decim(self) -> "DecimConfig | None":
        """The capture's decimation (None for a legacy 8-bit capture)."""
        if not self.decim_mode & DECIM_WIDE:
            return None
        return DecimConfig.from_status(self.decim_mode, self.decim_shift, self.decim_n)

    def channel_config(self, ch: int) -> "ChannelConfig":
        """The capture-time channel settings as a CHANNEL_CONFIG (dcoffset / center_cal are not carried)."""
        c = self.channels[ch]
        return ChannelConfig(channel=ch, enable=c.enable, range=c.range, hw_range=min(c.range, 5),
                             coupling=c.coupling, invert=c.invert, probe_index=c.probe_index, offset_pwm=c.offset_pwm,
                             uv_per_lsb_q8=c.uv_per_lsb_q8, zero_code_q8=c.zero_code_q8)

    def trigger(self) -> "Trigger":
        h = self.header
        per = 1 if self.decim is not None else 2  # 1.9: one 16-bit sample per ring word
        return Trigger(source=h.trig_source, edge=h.trig_edge, level=h.trig_level, mode=h.trig_mode,
                       pretrigger_samples=h.pretrigger_samples, total_samples=h.total_samples,
                       pretrigger_words=h.pretrigger_samples // per, total_words=h.total_samples // per)

    def timebase(self) -> "Timebase":
        d = self.decim
        if d is not None:  # output period N x 5 ns
            return Timebase(self.header.timeperdiv_index, 0xFF, self.header.divider, BASE_CLOCK_HZ // d.n,
                            min(5000 * d.n, 0xFFFFFFFF))
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
SD_FLAG_IDENT = 0x02  # protocol 1.16: identify the image at sector 750 (or reuse the cached identity)
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
    """SD_INFO response: 64 bytes (card, partition, write window, eGON headers at sectors 16, 80 and 750); 112 from
    protocol 1.16 with the card image's identity (``ident``, None on older firmware)."""

    card_sectors: int
    part_start: int
    write_end: int
    stage_bytes: int
    headers: list[SdHeaderInfo]
    ident: "SdIdent | None" = None

    def pack(self) -> bytes:
        return struct.pack("<IIII", self.card_sectors, self.part_start, self.write_end, self.stage_bytes) + \
            b"".join(h.pack() for h in self.headers) + (self.ident.pack() if self.ident is not None else b"")

    @classmethod
    def unpack(cls, data: bytes) -> "SdInfo":
        n = len(data)
        data = bytes(data) + b"\0" * max(0, 64 - len(data))
        a, b, c, d = struct.unpack_from("<IIII", data)
        ident = SdIdent.unpack(data[64:112]) if n >= 112 else None
        return cls(a, b, c, d, [SdHeaderInfo.unpack(data[16 + 16 * i: 32 + 16 * i]) for i in range(3)], ident)

    def header(self, sector: int) -> SdHeaderInfo | None:
        return next((h for h in self.headers if h.sector == sector), None)


#: SD_INFO byte 64 ident_state (protocol 1.16)
SD_IDENT_STATES = {0: "not read yet", 1: "identity record", 2: "commit hash and time from the build tail",
                   3: "version string only", 4: "unknown (no identity in the image)",
                   5: "no eGON.EXE image at sector 750, implausible length or read error"}
SD_IDENT_RUNNING = 0x01  # ident flags bit 0: the card image is the running image


def _time6(t: tuple[int, ...]) -> str | None:
    """yy mm dd hh mm ss (UTC) as ISO text; None for zeros."""
    if not any(t):
        return None
    return f"20{t[0]:02d}-{t[1]:02d}-{t[2]:02d}T{t[3]:02d}:{t[4]:02d}:{t[5]:02d}Z"


@dataclass
class SdIdent(Payload):
    """SD_INFO bytes 64..111 (protocol 1.16, row 55): the identity of the eGON.EXE image at sector 750."""

    FIELDS = [("state", "B"), ("flags", "B"), ("proto_major", "B"), ("proto_minor", "B"), ("version", "16s"),
              ("build", "I"), ("time", "6B"), ("_r0", "2x"), ("image_length", "I"), ("ident_ms", "I"), ("_r1", "8x")]
    state: int = 0
    flags: int = 0
    proto_major: int = 0
    proto_minor: int = 0
    version: bytes = bytes(16)
    build: int = 0
    time: tuple[int, ...] = field(default_factory=lambda: (0,) * 6)
    image_length: int = 0
    ident_ms: int = 0

    @property
    def version_str(self) -> str:
        return _cstr(self.version)

    @property
    def time_str(self) -> str | None:
        return _time6(self.time)

    @property
    def running(self) -> bool:
        return bool(self.flags & SD_IDENT_RUNNING)

    def describe(self) -> str:
        if self.state in (0, 4, 5):
            return SD_IDENT_STATES.get(self.state, f"state {self.state}")
        parts = [f"fw {self.version_str or '?'}"]
        if self.build:
            parts.append(f"build {self.build:08x}")
        if self.time_str:
            parts.append(f"committed {self.time_str}")
        if self.proto_minor:
            parts.append(f"protocol {self.proto_major}.{self.proto_minor}")
        return ", ".join(parts) + f" ({SD_IDENT_STATES[self.state]})"


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
        """Sample period: the device's period field (1.9: N x 5 ns while the decimation filter is on), or
        (divider + 1) x 5 ns when that field is saturated or absent."""
        if 0 < self.sample_period_ps < 0xFFFFFFFF:
            return self.sample_period_ps * 1e-12
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
    #: layouts: 0 per core, 1 both cores of a channel interleaved, 2 (1.9) u16 per word (low byte core A)
    LAYOUT_CORES: ClassVar[int] = 0
    LAYOUT_INTERLEAVED: ClassVar[int] = 1
    LAYOUT_WIDE: ClassVar[int] = 2


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
    #: 1.9: u16 samples of a decimated capture as the FPGA stored them (no compensation, no inversion)
    WIDE: ClassVar[int] = 0x04


@dataclass
class ReadTraceHeader(Payload):
    FIELDS = [("capture_id", "I"), ("sample_count", "I"), ("start", "I"), ("count", "I"),
              ("hw_trigger_index", "i"), ("sw_trigger_index", "i"), ("divider", "I"), ("ch_mask", "B"),
              ("origin", "B"), ("decim_mode", "B"), ("decim_shift", "B")]
    capture_id: int = 0
    sample_count: int = 0
    start: int = 0
    count: int = 0
    hw_trigger_index: int = 0
    sw_trigger_index: int = -1
    divider: int = 0
    ch_mask: int = 0
    origin: int = 0
    #: 1.9: the capture's mode (0x29 byte 0 layout; bit 7 = 16-bit capture) and S; 0 for legacy captures
    decim_mode: int = 0
    decim_shift: int = 0


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


# Decimation filter (protocol 1.9, FPGA 0x1534, section 14 row 47; mirrors firmware/scope/pc_decim.c)

#: SET_DECIM filter / GET_DECIM byte 0 (the UI's acquisition Filter setting)
DECIM_FILTERS = ("off", "boxcar", "cic2", "cic3")
#: filter choices with the 1.11 FIR (GET_DECIM byte 1 bit 3 / SET_DECIM flags bit 3), as the UI's Filter box steps
DECIM_FILTER_CHOICES = ("off", "boxcar", "boxcar+fir", "cic2", "cic2+fir", "cic3", "cic3+fir")


def parse_decim_filter(name: str) -> tuple[int, bool]:
    """'cic2+fir' -> (2, True); 'off' -> (0, False)."""
    n = name.strip().lower()
    if n not in DECIM_FILTER_CHOICES:
        raise ValueError(f"unknown filter {name!r}; use one of {', '.join(DECIM_FILTER_CHOICES)}")
    base, _, fir = n.partition("+")
    return DECIM_FILTERS.index(base), bool(fir)

#: 0x53 / 0x29 byte 0 bits besides K (bits 1:0)
DECIM_SWAP = 0x04  # core A is the earlier sample of each pair (host only, not saved)
DECIM_NEG_A = 0x08  # A cores captured on the falling write-clock edge
DECIM_NEG_B = 0x10
DECIM_FLAGS = DECIM_SWAP | DECIM_NEG_A | DECIM_NEG_B
DECIM_WIDE = 0x80  # 0x29 bit 7: 16-bit layout active (K >= 1)
DECIM_FIR_EN = 0x40  # 0x29 bit 6 (1.11): 0x56 FIR enable as written
DECIM_FIR_ACT = 0x20  # 0x29 bit 5 (1.11): FIR active now (enable, K >= 1, N >= 16, N % 4 == 0)
#: the firmware's Filter setting bit for the FIR (SET_DECIM flags bit 3, shifted like the other flags)
DECIM_SET_FIR = 0x20
DECIM_FIR_MIN_N = 16
#: decim_config capability levels: bitstream without the filter, 0x1534 (boxcar / CIC), 0x1535+ (plus the FIR)
DECIM_HAS_NONE, DECIM_HAS_CIC, DECIM_HAS_FIR = 0, 1, 2
#: largest capture (samples) in the 16-bit layout
DECIM_MAX_TOTAL = 8190
#: exactness 255 x N^K < 2^48: CIC3 up to N = 10,334, CIC2 up to 1,050,630
DECIM_CIC3_MAX_N = 10334
DECIM_CIC2_MAX_N = 1050630
#: GET_DECIM byte 30: why the effective K is lower than requested
DECIM_WHY = ("as requested", "filter off", "200 MSa/s (divider 0) is legacy only", "capture longer than 8190 samples",
             "CIC3 not exact at this N: CIC2 used", "CIC2 not exact at this N: boxcar used", "bitstream without the filter")


def decim_n(divider: int) -> int:
    """Decimation factor N = 2 x (floor(D / 2) + 1) for the 0x0D divider D."""
    return 2 * ((divider >> 1) + 1)


def decim_gain(n: int, k: int) -> int:
    return n ** k


def decim_shift(gain: int) -> int:
    """Output shift S = max(0, bitlen(255 x gain) - 16)."""
    return max(0, (255 * gain).bit_length() - 16)


def decim_level16(level8: int, gain: int, shift: int) -> int:
    """16-bit trigger level for an 8-bit one: round(level x N^K / 2^S), at most 65535."""
    y = level8 * gain
    if shift:
        y = (y + (1 << (shift - 1))) >> shift
    return min(y, 65535)


def decim_scale_q24(gain: int, shift: int) -> int:
    if gain == 0 or shift > 39:
        return 0
    return ((1 << (24 + shift)) + (gain >> 1)) // gain


def decim_to_code(out: int, scale_q24: int, corr16: int) -> int:
    """Firmware conversion of a stored 16-bit sample to the 8-bit trace: round(out x 2^S / N^K + corr16 / 16), 0..255.
    corr16 (1/16 code) is GET_CAL's with calibration v2, else 8 x (adc_a_comp + adc_b_comp) (fw 0.2.43)."""
    v = (out * scale_q24 + (corr16 << 20) + (1 << 23)) >> 24
    return max(0, min(255, v))


@dataclass
class DecimConfig:
    """Effective decimation for a setting (pc_decim_cfg): order K (0 = legacy 8-bit), flags, shift S, why (DECIM_WHY
    index), factor N (0 legacy), gain (N^K; (N/2)^K with the FIR active; 1 legacy), FIR enable as written (1.11) and
    whether the FIR is active."""

    k: int = 0
    flags: int = 0
    shift: int = 0
    why: int = 0
    n: int = 0
    gain: int = 1
    fir_en: bool = False
    fir: bool = False

    @property
    def mode(self) -> int:
        """The 0x53 byte."""
        return (self.k & 3) | self.flags

    @property
    def status(self) -> int:
        """The 0x29 byte 0 the FPGA reports."""
        return self.mode | (DECIM_WIDE if self.k else 0) | (DECIM_FIR_EN if self.fir_en else 0) | \
            (DECIM_FIR_ACT if self.fir else 0)

    @classmethod
    def from_status(cls, status: int, shift: int, n: int, gain: int | None = None, why: int = 0) -> "DecimConfig":
        """From a 0x29 byte 0 (GET_DECIM, GET_CAPTURE_INFO, READ_TRACE); the gain is (N/2)^K with the FIR active."""
        k = status & 3 if status & DECIM_WIDE else 0
        fir = bool(status & DECIM_FIR_ACT) and k > 0
        if gain is None:
            gain = decim_gain(n // 2 if fir else n, k) if k else 1
        return cls(k, status & DECIM_FLAGS, shift, why, n if k else 0, gain if k else 1, bool(status & DECIM_FIR_EN),
                   fir)

    @property
    def scale_q24(self) -> int:
        return decim_scale_q24(self.gain, self.shift) if self.k else 0

    @property
    def code_per_count(self) -> float:
        """ADC codes (mean of the filter window) per stored count: 2^S / gain."""
        return (1 << self.shift) / self.gain if self.k else 1.0

    def rate(self, divider: int) -> int:
        if self.k and self.n:
            return BASE_CLOCK_HZ // self.n
        return BASE_CLOCK_HZ // (divider + 1)

    @property
    def ext_shift(self) -> int:
        """EXT trigger: output samples by which the stored data lag the edge, rounded: 120 ns + K (N - 1) 2.5 ns;
        with the FIR 250 ns + 17.5 N ns + 1.25 K (N - 2) ns."""
        if not self.k or not self.n:
            return 0
        if self.fir:
            lag, period = 1000 + 70 * self.n + 5 * self.k * (self.n - 2), 20 * self.n
        else:
            lag, period = 240 + self.k * (self.n - 1) * 5, self.n * 10
        return (lag + period // 2) // period

    @property
    def settle_us(self) -> int:
        """Filter settling after a change: (K + 2) output periods, us, rounded up; with the FIR (K + 10) N 5 ns + 1 us."""
        if not self.k:
            return 0
        if self.fir:
            return min(((self.k + 10) * self.n + 199) // 200 + 1, 0xFFFFFFFF)
        return min(((self.k + 2) * self.n + 199) // 200, 0xFFFFFFFF)

    @property
    def filter_name(self) -> str:
        """Effective filter: 'cic2+fir' only while the FIR is active."""
        if not self.k:
            return "off"
        return DECIM_FILTERS[self.k] + ("+fir" if self.fir else "")


def decim_fir_eligible(n: int) -> bool:
    return n >= DECIM_FIR_MIN_N and n % 4 == 0


def decim_config(k_req: int, flags: int, divider: int, total_samples: int, has: int = DECIM_HAS_CIC) -> DecimConfig:
    """pc_decim_config: the configuration an arm writes for the Filter setting ``k_req`` (0-3), ``flags``
    (DECIM_FLAGS bits, plus DECIM_SET_FIR), the 0x0D divider, the capture length and the bitstream's level
    (DECIM_HAS_*). With the FIR active the CIC decimates by M = N / 2: gain M^K, S one higher, exactness on M."""
    k, n = k_req & 3, decim_n(divider)
    if has == DECIM_HAS_NONE:
        return DecimConfig(why=6)
    c = DecimConfig(flags=flags & DECIM_FLAGS, fir_en=has >= DECIM_HAS_FIR and bool(flags & DECIM_SET_FIR))
    if k == 0:
        c.why = 1
        return c
    if divider == 0:
        c.why = 2
        return c
    if total_samples > DECIM_MAX_TOTAL:
        c.why = 3
        return c
    c.fir = c.fir_en and decim_fir_eligible(n)
    m = n // 2 if c.fir else n
    if k == 3 and m > DECIM_CIC3_MAX_N:
        k, c.why = 2, 4
    if k == 2 and m > DECIM_CIC2_MAX_N:
        k, c.why = 1, 5
    c.k, c.n, c.gain = k, n, decim_gain(m, k)
    c.shift = decim_shift(c.gain) + (1 if c.fir else 0)
    return c


@dataclass
class SetDecimReq(Payload):
    """SET_DECIM: u8 filter (0 off, 1 boxcar, 2 CIC2, 3 CIC3), u8 flags (bit 0 core-order swap, bit 1 / 2 capture the
    A / B cores on the falling edge), u16 0."""

    FIELDS = [("filter", "B"), ("flags", "B"), ("_r", "2x")]
    filter: int = 0
    flags: int = 0

    SWAP: ClassVar[int] = 0x01
    NEG_A: ClassVar[int] = 0x02
    NEG_B: ClassVar[int] = 0x04
    #: 1.11 (caps FIR): the CIC compensation FIR
    FIR: ClassVar[int] = 0x08


@dataclass
class DecimState(Payload):
    """GET_DECIM / SET_DECIM response, 32 bytes."""

    FIELDS = [("filter", "B"), ("flags", "B"), ("next_mode", "B"), ("next_shift", "B"), ("next_n", "I"),
              ("next_gain", "Q"), ("divider", "I"), ("rate_hz", "I"), ("last_mode", "B"), ("last_shift", "B"),
              ("fpga_mode", "B"), ("fpga_shift", "B"), ("max_total", "H"), ("why", "B"), ("ext_shift", "B")]
    #: the setting: 0 off, 1 boxcar, 2 CIC2, 3 CIC3
    filter: int = 0
    #: SetDecimReq flag bits
    flags: int = 0
    #: what the next arm writes (0x29 byte 0 layout; 0 = legacy), its S, N and gain N^K
    next_mode: int = 0
    next_shift: int = 0
    next_n: int = 0
    next_gain: int = 1
    divider: int = 0
    #: output rate of the next arm
    rate_hz: int = 0
    #: written at the last arm (0xFF: none yet) and its S
    last_mode: int = 0xFF
    last_shift: int = 0
    #: FPGA 0x29 read now
    fpga_mode: int = 0
    fpga_shift: int = 0
    max_total: int = DECIM_MAX_TOTAL
    #: DECIM_WHY index
    why: int = 0
    #: EXT latency shift (samples) the next arm applies with the current source
    ext_shift: int = 0

    @property
    def next(self) -> DecimConfig:
        return DecimConfig.from_status(self.next_mode, self.next_shift, self.next_n, self.next_gain, self.why)

    @property
    def filter_setting(self) -> str:
        """The setting as the UI's Filter box names it ('cic2+fir')."""
        return DECIM_FILTERS[self.filter & 3] + ("+fir" if self.flags & SetDecimReq.FIR and self.filter else "")


@dataclass
class ReadSpectrumReq(Payload):
    """READ_SPECTRUM request (protocol 1.7): u8 channel, u8 0, u16 first bin, u16 count (0 = as many as fit), u16 0."""

    FIELDS = [("channel", "B"), ("_r0", "x"), ("first", "H"), ("count", "H"), ("_r1", "2x")]
    channel: int = 0
    first: int = 0
    count: int = 0


#: READ_SPECTRUM window codes
SPECTRUM_WINDOWS = {0: "rect", 1: "hann", 2: "flattop"}
#: READ_SPECTRUM level of an empty bin (centi-dB)
SPECTRUM_EMPTY = -32000


@dataclass
class SpectrumHeader(Payload):
    """READ_SPECTRUM response header, 32 bytes (section 14 row 45)."""

    FIELDS = [("channel", "B"), ("window", "B"), ("log2n", "B"), ("flags", "B"), ("frame", "I"), ("divider", "I"),
              ("sample_rate_hz", "I"), ("bin_width_mhz", "I"), ("gain_cdb", "i"), ("bins", "H"), ("first", "H"),
              ("count", "H"), ("enbw_milli", "H")]
    channel: int = 0
    window: int = 1
    log2n: int = 11
    flags: int = 0
    #: spectra computed since boot (changes with every new capture or setting)
    frame: int = 0
    divider: int = 0
    sample_rate_hz: int = 0
    #: fs / N in mHz, truncated
    bin_width_mhz: int = 0
    #: 20 log10(volts per code) in centi-dB, already applied to the levels
    gain_cdb: int = 0
    #: bins in the spectrum (N/2 + 1)
    bins: int = 0
    first: int = 0
    count: int = 0
    #: equivalent noise bandwidth in milli-bins (1000 rect, 1500 Hann, 3770 flat-top)
    enbw_milli: int = 1000

    F_AMPS: ClassVar[int] = 0x01

    @property
    def window_name(self) -> str:
        return SPECTRUM_WINDOWS.get(self.window, f"window{self.window}")


def parse_spectrum(payload: bytes) -> tuple[SpectrumHeader, tuple[int, ...]]:
    """READ_SPECTRUM: 32-byte header, then ``count`` i16 levels (centi-dBV / centi-dBA, SPECTRUM_EMPTY = empty)."""
    h = SpectrumHeader.unpack(payload[:32])
    n = min(h.count, max(0, len(payload) - 32) // 2)
    return h, struct.unpack_from(f"<{n}h", payload, 32)


def pack_spectrum(h: SpectrumHeader, levels: "list[int] | tuple[int, ...]") -> bytes:
    return h.pack() + struct.pack(f"<{len(levels)}h", *(int(v) for v in levels))


@dataclass
class FftBenchEntry(Payload):
    """One FFT_BENCH entry (protocol 1.6, 24 bytes; section 14 row 44). Times in us."""

    FIELDS = [("n", "H"), ("window", "B"), ("memory", "B"), ("init_us", "I"), ("run_min_us", "I"),
              ("run_max_us", "I"), ("db_min_us", "I"), ("peak_bin", "H"), ("peak_cdb", "h")]
    n: int = 0
    window: int = 1
    #: 0 DDR, 1 on-chip SRAM A
    memory: int = 0
    #: plan init (window table)
    init_us: int = 0
    #: window + load + FFT + split step, over the reps
    run_min_us: int = 0
    run_max_us: int = 0
    #: dB conversion of the N/2 + 1 bins
    db_min_us: int = 0
    #: bins 2..N/2
    peak_bin: int = 0
    #: centi-dB re 1 code RMS (a full-scale sine reads about 3907)
    peak_cdb: int = 0


@dataclass
class FftBench:
    reps: int
    #: CP15 control register as read: bit 0 MMU, bit 2 D-cache, bit 12 I-cache
    cp15: int
    entries: list[FftBenchEntry]


def parse_fft_bench(payload: bytes) -> FftBench:
    """FFT_BENCH: u16 n, u16 reps, u32 CP15 control, then n x 24-byte entries."""
    n, reps, cp15 = struct.unpack_from("<HHI", payload)
    n = min(n, (len(payload) - 8) // 24)
    return FftBench(reps, cp15, [FftBenchEntry.unpack(payload[8 + 24 * i: 32 + 24 * i]) for i in range(n)])


def pack_fft_bench(b: FftBench) -> bytes:
    return struct.pack("<HHI", len(b.entries), b.reps, b.cp15) + b"".join(e.pack() for e in b.entries)


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


#: GET_CAL byte 73 (protocol 1.10, bits 3 / 4 from 1.12)
CAL_LOADED = 0x01  # a calibration block is loaded
CAL_BOUND = 0x02  # bound to the current PWM centres (dc_calibration_offset[0..5])
CAL_GATED = 0x04  # the current divider is gated (legacy divider >= 1): the gated class applies
CAL_GAINS_MEASURED = 0x08  # 1.12: gains measured for this channel (gain_range != 0xFF)
CAL_GAINS_KEPT = 0x10  # 1.12: the gains were carried over from an earlier calibration
#: GET_CAL byte 256 track_flags (protocol 1.15, row 54)
TRACK_RECORD = 0x01  # a record was accepted since the reset (boot or a calibration)
TRACK_E = 0x02  # e identifiable at the last solve (spread of x >= 8 codes RMS)
TRACK_Q = 0x04  # q identifiable at the last solve
TRACK_LAST_USABLE = 0x08  # the last offered record was usable
TRACK_APPLIES = 0x10  # applies to the channel's next 8-bit readout (v3 bound, full class, last arm legacy K = 0)
TRACK_PROBE_DATA = 0x20  # a valid class probe since the reset
TRACK_PROBE_VALID = 0x40  # the last class probe was valid
#: GET_CAL ages: none since the reset
CAL_AGE_NONE = 0xFFFFFFFF
#: clock classes (row 48): 0 full rate (legacy divider 0 and every filtered capture), 1 gated (legacy divider >= 1)
CAL_CLASS_FULL = 0
CAL_CLASS_GATED = 1
CAL_CLASS_NAMES = ("full", "gated")
#: v3 gain_range when the gains were never measured (gains 32768 = 1)
CAL_GAIN_NONE = 0xFF


def cal_clock_class(divider: int, decimated: bool) -> int:
    """The clock class of a capture: full at divider 0 and for every filtered (16-bit) capture, else gated."""
    return CAL_CLASS_FULL if divider == 0 or decimated else CAL_CLASS_GATED


def _rdiv(n: int, d: int) -> int:
    """The firmware's rdiv / rdiv64: round half away from zero."""
    return (n + d // 2) // d if n >= 0 else -((-n + d // 2) // d)


def cal_target(dcoffset: int, dc_shift_value: int) -> int:
    """T = 128 - trunc(dcoffset x 100 / dc_shift_value): the trace code of 0 V under calibration v3 (C division)."""
    if not dc_shift_value:
        return 128
    q = abs(dcoffset * 100) // abs(dc_shift_value)
    return 128 - (q if (dcoffset * 100 >= 0) == (dc_shift_value > 0) else -q)


@dataclass
class CalResp(Payload):
    """GET_CAL: 72 bytes up to protocol 1.9; 1.12 (fw 0.2.45, row 51): 256 bytes with the calibration v3 state, the
    next readout's per-core gains and offsets, and the channel's stored v3 block; 1.15 (fw 0.2.50, row 54): 288 bytes,
    the v4 tracker (A - B = d + e x + q x^2 of the full-rate cores, and the full class's common mode cm against the
    gated class) from byte 256, and gain_a .. off_b / corr16 then include it. (1.10-1.11 sent a 248-byte v2 layout,
    which this host no longer decodes: ``cal_version`` 2 is refused by the volts model.) Shorter answers parse with
    the missing tail zero; ``nbytes`` is the length received (0 for a locally built one)."""

    FIELDS = [("channel", "B"), ("_r0", "3x"), ("dc_calibration_offset", "7H"), ("_r1", "2x"),
              ("input_calibration", "7i"), ("adc_a_comp", "h"), ("adc_b_comp", "h"), ("dc_shift_center", "I"),
              ("dc_shift_size", "i"), ("dc_shift_value", "i"), ("adc_a_rawavg", "I"), ("adc_b_rawavg", "I"),
              ("cal_version", "B"), ("cal_flags", "B"), ("comp_a", "h"), ("comp_b", "h"), ("frac_a", "B"),
              ("frac_b", "B"), ("cal_zero_q8", "i"), ("corr16", "i"), ("cal_shift", "i"), ("gain_a", "I"),
              ("gain_b", "I"), ("off_a", "i"), ("off_b", "i"), ("gain16", "I"), ("pwm", "6H"), ("zero", "24h"),
              ("slope", "24h"), ("noise", "24B"), ("gain", "4H"), ("gain_range", "B"), ("gain_flags", "B"),
              ("gain_lever", "H"), ("track_flags", "B"), ("_r2", "x"), ("track_d_q8", "h"), ("track_e_q24", "i"),
              ("track_q_q24", "i"), ("track_age_ms", "I"), ("track_records", "I"), ("track_last_pairs", "H"),
              ("track_spread16", "H"), ("cm_q8", "h"), ("cm_probes", "H"), ("cm_age_ms", "I"),
              ("prov_state", "B"), ("_r3", "3x"), ("prov", "56s"), ("prov_power_ons_since", "I"),
              ("prov_minutes_since", "I")]
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
    #: calibration in use for this channel (1 = the 1.00o5 compensations, 3 = v3; 0 = firmware before 1.10)
    cal_version: int = 0
    #: CAL_LOADED / CAL_BOUND / CAL_GATED / CAL_GAINS_MEASURED / CAL_GAINS_KEPT
    cal_flags: int = 0
    #: the correction at each core's 0 V point (T16 - z16) in whole codes and sixteenths
    comp_a: int = 0
    comp_b: int = 0
    frac_a: int = 0
    frac_b: int = 0
    #: trace code of 0 V x 256 for the current settings
    cal_zero_q8: int = 0
    #: 16-bit path correction in 1/16 code: code = out x 2^S / gain x gain16 / 65536 + corr16 / 16
    corr16: int = 0
    #: dcoffset x 100 / dc_shift_value (what the firmware measurements add)
    cal_shift: int = 0
    #: 1.12: the next 8-bit readout: code_k = (raw x gain_k + off_k + dither) >> 16 (Q16), and the 16-bit path gain
    gain_a: int = 65536
    gain_b: int = 65536
    off_a: int = 0
    off_b: int = 0
    gain16: int = 65536
    #: the stored v3 block: PWM centres it was measured at, zero[class][core][range] (1/16 code, raw codes at
    #: dcoffset 0), slope[class][core][range] (Q16 codes per PWM count), noise[class][core][range] (RMS, 1/16 code),
    #: gain[class][core] (Q15, 32768 = 1), the hardware range the gains were measured on (CAL_GAIN_NONE = never),
    #: gain_flags (bit 0 carried over), gain_lever (gated span of the mean of both cores, 1/16 code)
    pwm: tuple[int, ...] = field(default_factory=lambda: (0,) * 6)
    zero: tuple[int, ...] = field(default_factory=lambda: (0,) * 24)
    slope: tuple[int, ...] = field(default_factory=lambda: (0,) * 24)
    noise: tuple[int, ...] = field(default_factory=lambda: (0,) * 24)
    gain: tuple[int, ...] = field(default_factory=lambda: (32768,) * 4)
    gain_range: int = CAL_GAIN_NONE
    gain_flags: int = 0
    gain_lever: int = 0
    #: 1.15 (row 54): TRACK_* bits; d (1/256 code), e and q (Q24) of A - B = d + e x + q x^2 (x = code - 128; d and e
    #: applied in the full class, q only reported); age (ms, CAL_AGE_NONE = none since the reset), accepted records,
    #: usable pairs of the last offered record, RMS spread of the accumulated x (1/16 code); the full class's common
    #: mode against the gated class (1/256 code, subtracted by the readouts), valid probes and the last one's age
    track_flags: int = 0
    track_d_q8: int = 0
    track_e_q24: int = 0
    track_q_q24: int = 0
    track_age_ms: int = CAL_AGE_NONE
    track_records: int = 0
    track_last_pairs: int = 0
    track_spread16: int = 0
    cm_q8: int = 0
    cm_probes: int = 0
    cm_age_ms: int = CAL_AGE_NONE
    #: 1.16 (row 55): calibration provenance: 0 none on the card, 1 valid, 2 present but unreadable; the stored
    #: 56 bytes (sector 708 bytes 432..487, parse_provenance); power-ons and operating minutes since (CAL_AGE_NONE =
    #: unknown)
    prov_state: int = 0
    prov: bytes = bytes(56)
    prov_power_ons_since: int = CAL_AGE_NONE
    prov_minutes_since: int = CAL_AGE_NONE
    nbytes: int = field(default=0, compare=False)

    @classmethod
    def unpack(cls, data: bytes) -> "CalResp":
        r = super().unpack(data)
        r.nbytes = len(data)
        return r

    @property
    def v3(self) -> bool:
        """Calibration v3 in use for this channel."""
        return self.cal_version == 3

    @property
    def has_tracker(self) -> bool:
        """The answer carries the 1.15 tracker block (288 bytes or more)."""
        return self.nbytes >= 288

    @property
    def has_provenance(self) -> bool:
        """The answer carries the 1.16 provenance block (356 bytes)."""
        return self.nbytes >= 356

    def provenance(self) -> "Provenance | None":
        """The stored provenance when prov_state is 1 (valid), else None."""
        if self.prov_state != 1:
            return None
        return parse_provenance(self.prov)

    @property
    def track_d(self) -> float:
        """A - B offset at code 128 the full-class readouts take out, codes."""
        return self.track_d_q8 / 256.0

    @property
    def track_e(self) -> float:
        """A - B slope (codes per code) the full-class readouts take out."""
        return self.track_e_q24 / 16777216.0

    @property
    def track_q(self) -> float:
        """The bow estimate (codes per code^2), reported, not applied."""
        return self.track_q_q24 / 16777216.0

    @property
    def track_cm(self) -> float:
        """Full-class common mode against the gated class, codes (subtracted from full-class codes)."""
        return self.cm_q8 / 256.0

    @staticmethod
    def _i(cls_: int, core: int, rng: int) -> int:
        return 12 * cls_ + 6 * core + min(rng, 5)

    def zero_raw(self, cls: int, core: int, rng: int) -> float:
        """Stored raw 0 V code of core (0 A, 1 B) at dcoffset 0, codes."""
        return self.zero[self._i(cls, core, rng)] / 16.0

    def slope_codes(self, cls: int, core: int, rng: int) -> float:
        """Stored offset slope, codes per PWM count."""
        return self.slope[self._i(cls, core, rng)] / 65536.0

    def noise_rms(self, cls: int, core: int, rng: int) -> float:
        return self.noise[self._i(cls, core, rng)] / 16.0

    def zero16_at(self, cls: int, core: int, rng: int, dcoffset: int) -> int:
        """z16 = zero + round(slope x dcoffset / 4096), 1/16 code (adc_cal.c acal_correction)."""
        i = self._i(cls, core, rng)
        return self.zero[i] + _rdiv(self.slope[i] * dcoffset, 4096)

    def zero_at(self, cls: int, core: int, rng: int, dcoffset: int) -> float:
        """Raw 0 V code of a core at ``dcoffset`` (raw uncompensated codes, the READ_RAW reference)."""
        return self.zero16_at(cls, core, rng, dcoffset) / 16.0

    def gain_q16(self, cls: int, core: int) -> int:
        """G_k[c] in Q16 (acal_gain16; 65536 when the gains were never measured)."""
        return 65536 if self.gain_range == CAL_GAIN_NONE else 2 * self.gain[2 * cls + core]

    def gain_corr(self, cls: int, core: int) -> float:
        """G_k[c] = g_ref / g_k[c]: the factor that puts core k of class c onto the gated mean gain."""
        return self.gain_q16(cls, core) / 65536.0

    def gain16_full(self) -> int:
        """16-bit path gain (Q16): the harmonic mean of the full-class gains of both cores, as the firmware rounds it."""
        a, b = self.gain_q16(CAL_CLASS_FULL, 0), self.gain_q16(CAL_CLASS_FULL, 1)
        return (2 * a * b + (a + b) // 2) // (a + b)


@dataclass
class AdcCalChannel:
    """One channel of the calibration v3 block (sector 708 bytes 128..431), decoded like GET_CAL bytes 112..255."""

    pwm: tuple[int, ...]
    zero: tuple[int, ...]
    slope: tuple[int, ...]
    noise: tuple[int, ...]
    gain: tuple[int, ...]
    gain_range: int
    gain_flags: int
    gain_lever: int


# --- protocol 1.16 stored layouts (row 55; docs/protocol-vectors.json diag_layouts) ---------------------------

PROV_MAGIC = 0x56525041  # 'APRV'
IDENT_MAGIC = (0x44495746, 0x33313031)  # 'FWID', '1013'
LIFE_MARKER = 0x11FE
_PROV = struct.Struct("<IHHI16sHBBIII6BH")
#: provenance 'what' bits: the parts the calibration wrote
PROV_WHAT = ((0x01, "CH1 zeros"), (0x02, "CH1 gains"), (0x04, "CH2 zeros"), (0x08, "CH2 gains"))
#: exception types of the restart record (row 38)
EXCEPTION_TYPES = ("jump to the image entry", "undefined instruction", "SWI", "prefetch abort", "data abort",
                   "reserved vector", "FIQ")


@dataclass
class Provenance:
    """Calibration provenance (sector 708 bytes 432..487, GET_CAL 292..347)."""

    build: int
    version: str
    fpga: int
    what: int
    rtc: bool
    uptime_ms: int
    power_on: int
    minutes: int
    time: tuple[int, ...]

    @property
    def parts(self) -> list[str]:
        return [n for b, n in PROV_WHAT if self.what & b]

    @property
    def time_str(self) -> str | None:
        return _time6(self.time) if self.rtc else None


def parse_provenance(b: bytes) -> Provenance:
    """The 56-byte trailer; ValueError on a wrong magic, version, length or CRC."""
    if len(b) < 56:
        raise ValueError("provenance shorter than 56 bytes")
    v = _PROV.unpack_from(b)
    magic, ver, length = v[0], v[1], v[2]
    if magic != PROV_MAGIC or ver != 1 or length != 56:
        raise ValueError(f"not a provenance trailer (magic 0x{magic:08X}, version {ver}, length {length})")
    (crc,) = struct.unpack_from("<I", b, 52)
    if crc != zlib.crc32(bytes(b[:52])):
        raise ValueError("provenance CRC mismatch")
    return Provenance(v[3], _cstr(v[4]), v[5], v[6], bool(v[7]), v[8], v[9], v[10], tuple(v[11:17]))


def pack_provenance(p: Provenance) -> bytes:
    body = _PROV.pack(PROV_MAGIC, 1, 56, p.build, p.version.encode(), p.fpga, p.what, int(p.rtc), p.uptime_ms,
                      p.power_on, p.minutes, *p.time, 0)
    return body + struct.pack("<I", zlib.crc32(body))


@dataclass
class ImageIdentity:
    """The identity record fw 0.2.52+ links last in the image (36 bytes)."""

    version: str
    build: int
    time: tuple[int, ...]
    proto: tuple[int, int]


_IDENT = struct.Struct("<II16sI6BBB")


def parse_ident_record(b: bytes) -> ImageIdentity:
    v = _IDENT.unpack_from(b)
    if (v[0], v[1]) != IDENT_MAGIC:
        raise ValueError("not an identity record")
    return ImageIdentity(_cstr(v[2]), v[3], tuple(v[4:10]), (v[10], v[11]))


def pack_ident_record(i: ImageIdentity) -> bytes:
    return _IDENT.pack(*IDENT_MAGIC, i.version.encode(), i.build, *i.time, *i.proto)


_LIFE = struct.Struct("<HHIIHHHHI10I")


def parse_life_record(b: bytes) -> dict[str, Any]:
    """Settings sector 709 words 224..255 (64 bytes): the unit-life record and the stored restart record."""
    v = _LIFE.unpack_from(b)
    if v[0] != LIFE_MARKER:
        raise ValueError(f"no life record (marker 0x{v[0]:04X})")
    rec = None
    if v[4] & 1:
        rec = {"type": v[6], "restarts": v[5], "power_on": v[8], "exc_lr": v[9], "spsr": v[10], "exc_sp": v[11],
               "reset_lr": v[12], "reset_sp": v[13], "reset_cpsr": v[14], "fsr": v[15], "far": v[16],
               "minutes": v[17]}
    return {"flags": v[1], "power_on": v[2], "minutes": v[3], "restart": rec}


def pack_life_record(d: dict[str, Any]) -> bytes:
    r = d.get("restart")
    if r is None:
        return _LIFE.pack(LIFE_MARKER, d["flags"], d["power_on"], d["minutes"], 0, 0, 0, 0, 0, *([0] * 10))
    return _LIFE.pack(LIFE_MARKER, d["flags"], d["power_on"], d["minutes"], 1, r["restarts"], r["type"], 0,
                      r["power_on"], r["exc_lr"], r["spsr"], r["exc_sp"], r["reset_lr"], r["reset_sp"],
                      r["reset_cpsr"], r["fsr"], r["far"], r["minutes"], 0)


#: GET_HEALTH request flags (protocol 1.16)
HEALTH_CLEAR_RESTART = 0x01
HEALTH_RESTART_TEST = 0x80  # PC_DEBUG builds only (caps DEBUG_RAW)
USB_STATES = ("off", "reconnecting", "not configured", "configured", "suspended")
USB_STATE_TEXT = (
    "off: USB switched off by other code (mass-storage mode or ENTER_FEL)",
    "reconnecting: the suspend watch's soft disconnect is in progress",
    "not configured: pull-up on, no SET_CONFIGURATION since the last bus reset (no host, a charge-only cable or "
    "enumeration in progress)",
    "configured: enumerated, bus active",
    "suspended: the bus went idle (host asleep, selective suspend, or the cable pulled)")
LAST_END = ("unknown (no life record)", "settings saved at power-off",
            "ended without a power-off save (warm restart, power lost, a viewer open at power-off or the write failed)")


@dataclass
class HealthResp(Payload):
    """GET_HEALTH response, 96 bytes (protocol 1.16, row 55)."""

    FIELDS = [("layout", "B"), ("_r0", "x"), ("length", "H"), ("usb_state", "B"), ("usb_flags", "B"),
              ("usb_power", "B"), ("usb_faddr", "B"), ("suspend_ms", "I"), ("since_config_ms", "I"), ("cp15", "I"),
              ("power_on", "I"), ("minutes", "I"), ("uptime_ms", "I"), ("life_flags", "B"), ("last_end", "B"),
              ("settings_flags", "B"), ("saves", "B"), ("last_save_result", "i"), ("settings_eval_ms", "I"),
              ("rec_flags", "B"), ("rec_type", "B"), ("rec_restarts", "H"), ("rec_power_on", "I"),
              ("rec_exc_lr", "I"), ("rec_spsr", "I"), ("rec_exc_sp", "I"), ("rec_reset_lr", "I"),
              ("rec_reset_sp", "I"), ("rec_reset_cpsr", "I"), ("rec_fsr", "I"), ("rec_far", "I"),
              ("rec_minutes", "I"), ("_r1", "8x")]
    layout: int = 1
    length: int = 96
    usb_state: int = 3
    usb_flags: int = 0
    usb_power: int = 0
    usb_faddr: int = 0
    suspend_ms: int = 0
    since_config_ms: int = 0xFFFFFFFF
    cp15: int = 0
    power_on: int = 0
    minutes: int = 0
    uptime_ms: int = 0
    life_flags: int = 0
    last_end: int = 0
    settings_flags: int = 0
    saves: int = 0
    last_save_result: int = 1
    settings_eval_ms: int = 0
    rec_flags: int = 0
    rec_type: int = 0
    rec_restarts: int = 0
    rec_power_on: int = 0
    rec_exc_lr: int = 0
    rec_spsr: int = 0
    rec_exc_sp: int = 0
    rec_reset_lr: int = 0
    rec_reset_sp: int = 0
    rec_reset_cpsr: int = 0
    rec_fsr: int = 0
    rec_far: int = 0
    rec_minutes: int = 0

    def describe(self) -> dict[str, Any]:
        """The answer in words and units (what the Diagnostics pages show)."""
        uf = self.usb_flags
        usb = {"state": USB_STATES[self.usb_state] if self.usb_state < len(USB_STATES) else self.usb_state,
               "text": USB_STATE_TEXT[self.usb_state] if self.usb_state < len(USB_STATE_TEXT) else "",
               "high_speed": bool(uf & 1), "configured": bool(uf & 2), "suspended": bool(uf & 4),
               "port_open": bool(uf & 8), "soft_connect": bool(uf & 16), "power_reg": f"0x{self.usb_power:02X}",
               "address": self.usb_faddr, "suspended_s": round(self.suspend_ms / 1000, 3) if self.suspend_ms else 0,
               "since_configured_s": None if self.since_config_ms == 0xFFFFFFFF else
               round(self.since_config_ms / 1000, 3)}
        c = self.cp15
        cpu = {"cp15_control": f"0x{c:08X}", "mmu": bool(c & 1), "dcache": bool(c & 4), "icache": bool(c & 0x1000)}
        lf, sf = self.life_flags, self.settings_flags
        boot_write = "succeeded" if lf & 2 else "failed" if lf & 4 else "none (the settings sector was not valid)"
        life = {"power_on": self.power_on or None, "operating_hours": round(self.minutes / 60, 2),
                "uptime_s": round(self.uptime_ms / 1000, 1), "life_record_found": bool(lf & 1),
                "settings_sector_valid_at_boot": bool(lf & 8), "boot_write": boot_write,
                "previous_power_on_ended": LAST_END[self.last_end] if self.last_end < 3 else self.last_end}
        settings = {"changed_since_saved": bool(sf & 1), "last_save_failed": bool(sf & 2),
                    "saves_this_power_on": self.saves,
                    "last_save_result": None if self.last_save_result == 1 else self.last_save_result,
                    "evaluated_s_ago": round(self.settings_eval_ms / 1000, 1)}
        rec = None
        if self.rec_flags & 1:
            t = self.rec_type
            rec = {"from_this_power_on": bool(self.rec_flags & 2),
                   "exception": EXCEPTION_TYPES[t] if t < len(EXCEPTION_TYPES) else f"type {t}",
                   "restarts": self.rec_restarts, "power_on": self.rec_power_on,
                   "operating_hours": round(self.rec_minutes / 60, 2),
                   "registers": {k: f"0x{getattr(self, 'rec_' + k):08X}" for k in
                                 ("exc_lr", "spsr", "exc_sp", "reset_lr", "reset_sp", "reset_cpsr", "fsr", "far")}}
        return {"usb": usb, "cpu": cpu, "life": life, "settings": settings, "restart_record": rec}


@dataclass
class SaveReportResp(Payload):
    """SAVE_REPORT response, 56 bytes (protocol 1.16)."""

    FIELDS = [("bytes_written", "I"), ("_r0", "2x"), ("name_len", "H"), ("name", "48s")]
    bytes_written: int = 0
    name_len: int = 0
    name: bytes = b""

    @property
    def name_str(self) -> str:
        return _cstr(self.name)


ADC_CAL_MAGIC = b"ACAL"
_ADC_CAL_CH = struct.Struct("<6H24h24h24B4HBBH")


def parse_adc_cal_block(block: bytes) -> list[AdcCalChannel]:
    """The v3 block: u32 'ACAL', u16 version 3, u16 length 304, u16 classes 2, u16 0, two channels of 144 bytes,
    u32 zlib.crc32 of the 300 bytes before it. Raises ValueError when the header or the CRC is wrong."""
    if len(block) < 304 or block[:4] != ADC_CAL_MAGIC:
        raise ValueError("not a calibration block")
    ver, length, classes, _r = struct.unpack_from("<HHHH", block, 4)
    if ver != 3 or length != 304 or classes != 2:
        raise ValueError(f"calibration block version {ver}, length {length}, classes {classes} (host decodes v3)")
    (crc,) = struct.unpack_from("<I", block, 300)
    if crc != zlib.crc32(block[:300]):
        raise ValueError("calibration block CRC mismatch")
    out = []
    for i in range(2):
        v = _ADC_CAL_CH.unpack_from(block, 12 + 144 * i)
        out.append(AdcCalChannel(v[:6], v[6:30], v[30:54], v[54:78], v[78:82], v[82], v[83], v[84]))
    return out


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


#: Counters per firmware (protocol.md row 38): 27 (0.1.4-0.1.7), 29 (0.2.0-0.2.1), 31 (0.2.2-0.2.3), 35 (0.2.4),
#: 41 (0.2.5-0.2.6), 47 (0.2.7). fw 0.2.4+: 29..34 are "ms + 1 after the first main-loop poll, 0 = not reached"; in 0.2.2-0.2.3
#: (31 counters) 29/30 were first_arm_ms / first_done_ms ("ms | 1")
LINK_STATS_NAMES_35 = LINK_STATS_NAMES[:29] + (
    "first_conversion_ms", "first_capture_ms", "first_after_battery_ms", "first_after_acquisition_ms",
    "first_after_touch_ms", "first_after_rtc_ms",
    # fw 0.2.5+ (41 counters): USB device events
    "usb_bus_resets", "usb_suspends", "usb_resumes", "usb_disconnects", "ep0_setups", "ep0_bad_setups",
    # fw 0.2.7 (47 counters): RX back-pressure diagnostics (names as in firmware/scope/host_tests/bench_link.py)
    "rx_isr_stalls", "rx_releases", "rx_restalls", "rx_empty_releases", "rx_count_max", "rx_count_oversize",
    # fw 0.2.12-0.2.20 (48 counters): timer polls that served USB interrupt flags; retired in fw 0.2.21 (the tick
    # fallback was removed: the dispatcher never lost interrupts), always 0 there
    "usb_irq_polled",
    # fw 0.2.17 (49 counters): ms the boot splash step took + 1 (0 = not run)
    "boot_splash_ms",
    # fw 0.2.19 (57 counters): restart record since the last cold boot / ENTER_FEL (the lead maps PCs with the ELF)
    "restarts", "restart_exc_type", "restart_exc_lr", "restart_exc_spsr", "restart_exc_sp", "restart_reset_lr",
    "restart_reset_sp", "restart_reset_cpsr",
    # fw 0.2.20 (58 counters): ticks where the quiet-tick gate would have served USB flags but the USB IRQ was
    # pending at the INTC (the tick racing the real handler); retired in fw 0.2.21, always 0 there
    "usb_irq_races",
    # fw 0.2.48 (60 counters): fault status (DFSR for a data abort, IFSR for a prefetch abort) and fault address (FAR,
    # data abort only) of the restart recorded in 49-56; 0 otherwise
    "restart_fault_status", "restart_fault_addr",
    # fw 0.2.49 (62 counters): USB resumes inferred without a RESUME interrupt (frame number moving, EP0 setup or EP2
    # OUT packet while suspended), soft reconnects of the suspend watch (protocol row 53)
    "usb_inferred_resumes", "usb_soft_reconnects")

#: restart_exc_type / event 15 a (fw 0.2.19)
RESTART_EXC_TYPES = {0: "jump to entry", 1: "undefined", 2: "SWI", 3: "prefetch abort", 4: "data abort",
                     5: "reserved", 6: "FIQ"}
#: GET_LINK_STATS counters of the restart record (printed raw, in hex, by fnirsi1013d-soak)
RESTART_RECORD = ("restarts", "restart_exc_type", "restart_exc_lr", "restart_exc_spsr", "restart_exc_sp",
                  "restart_reset_lr", "restart_reset_sp", "restart_reset_cpsr", "restart_fault_status",
                  "restart_fault_addr")


#: GET_EVENT_LOG request flags (fw 0.2.18): bit 0 re-arms the reset snapshot after this read (a plain read keeps it;
#: 0.2.15-0.2.17 re-armed on every read and ignore the byte)
EVENT_LOG_REARM = 0x01

#: GET_EVENT_LOG entry types (protocol 1.5, section 14 row 42)
EVENT_TYPES = {1: "bus_reset", 2: "suspend", 3: "resume", 4: "disconnect", 5: "ep0_setup", 6: "ep0_short_packet",
               7: "set_configuration", 8: "clear_halt", 9: "dtr", 10: "rx_ring_full", 11: "rx_release",
               12: "tx_flushed", 13: "tx_aborted", 14: "usb_irq_polled", 15: "restart", 16: "usb_init_state",
               17: "tx_abort_txcsr", 18: "tx_abort_frame", 19: "usb_postinit_state", 20: "usb_soft_disconnect"}
#: event types the fw 0.2.24 bus-level ring keeps (PC_EVBUS_TYPES)
BUS_EVENT_TYPES = frozenset({1, 2, 3, 4, 7, 8, 12, 13, 15, 16, 17, 18, 20})
#: PHY tuning bits as fw 0.2.25 writes them (event 19 fields 4/5: bit 0 PHY 0x0C, bits 1-5 PHY 0x20-0x24, bits 6-7
#: PHY 0x2A-0x2B)
PHY_TUNING_WRITTEN = 0x00E9


def _ccu_flags(f: int) -> str:
    names = ("otg_reset_released", "otg_clock_on", "phy_reset_released", "phy_clock_on")
    return ",".join(n for i, n in enumerate(names) if f & (1 << i)) or "none"


def _phy_tuning(b: int) -> str:
    return (f"0x{b:04X} (res45_cal {b & 1}, tx_amp 0x{(b >> 1) & 0x1F:02X}, disc_thresh {(b >> 6) & 3}"
            + (", as written)" if b == PHY_TUNING_WRITTEN else ")"))


def _usb_state_field(t: int, a: int, b: int) -> str:
    """Events 16 (fw 0.2.22, controller state found at usb_device_init) and 19 (fw 0.2.25, at its end)."""
    if a == 0:
        return f"POWER 0x{b >> 8:02X} FADDR {b & 0x7F}"
    if a == 1:
        return f"INTUSBE 0x{b >> 8:02X} ccu {_ccu_flags(b & 0xFF)}"
    if t == 16:
        if a == 2:
            return f"EP2 TXCSR 0x{b:04X}"
        if a == 3:
            return f"EP2 RXCSR 0x{b:04X}"
        if a == 4:
            return f"ISCR 0x{b << 4:06X} (bits 4-19)"
    else:
        if a == 2:
            return f"ISCR 0x{b << 4:06X} (bits 4-19)"
        if a == 3:
            return f"USBPHY_CFG 0x{b:04X}"
        if a == 4:
            return "PHY tuning now " + _phy_tuning(b)
        if a == 5:
            return "PHY tuning after reset " + _phy_tuning(b)
        if a == 6:
            return f"PHYCTL 0x{b:04X}"
    return f"field {a} 0x{b:04X}"


@dataclass
class DeviceEvent:
    ms: int
    type: int
    a: int
    b: int

    @property
    def name(self) -> str:
        return EVENT_TYPES.get(self.type, f"type{self.type}")

    def describe(self) -> str:
        t, a, b = self.type, self.a, self.b
        if t in (1, 4):  # fw 0.2.44: a = suspended before, b = ms since that suspend (65535 = longer)
            if not a:
                return self.name
            return f"{self.name} after suspend {'>= 65535' if b == 0xFFFF else b} ms"
        if t == 2:  # fw 0.2.44: a = POWER, b = frame number (both 0 before)
            return f"suspend POWER 0x{a:02X} frame {b & 0x7FF}" if a or b else self.name
        if t == 3:  # fw 0.2.44: a = POWER (bit 7 = inferred, no RESUME interrupt), b = ms suspended
            if not (a or b):
                return self.name
            if not a & 0x80:
                return f"resume after {b} ms suspended, RESUME interrupt, POWER 0x{a & 0x7F:02X}"
            cause = (a >> 5) & 3  # fw 0.2.49: bits 6-5 cause, bits 4-0 POWER; 3 = fw 0.2.44-0.2.48 (POWER bits 6-0)
            if cause == 3:
                return f"resume after {b} ms suspended, inferred from SOFs (no RESUME interrupt), POWER 0x{a & 0x7F:02X}"
            how = ("SOFs", "an EP0 setup", "an EP2 OUT packet")[cause]
            return f"resume after {b} ms suspended, inferred from {how} (no RESUME interrupt), POWER bits 0x{a & 0x1F:02X}"
        if t == 20:  # fw 0.2.49: a = reason, storm level << 2, suspends since the last proof of life << 4; b = s
            why = {1: "retry storm", 2: "long suspend"}.get(a & 3, f"reason {a & 3}")
            return (f"usb_soft_disconnect ({why}) after {'>= 65535' if b == 0xFFFF else b} s suspended, "
                    f"{a >> 4} suspends, back-off level {(a >> 2) & 3}")
        if t == 5:
            return f"ep0_setup bRequest 0x{a:02X} bmRequestType 0x{b >> 8:02X} wValue.lo 0x{b & 0xFF:02X}"
        if t == 6:
            return f"ep0_short_packet COUNT0 {b}"
        if t == 8:
            return f"clear_halt ep 0x{a:02X}"
        if t == 9:
            return f"dtr {a}"
        if t == 10:
            return f"rx_ring_full RXCOUNT {b}"
        if t in (12, 13):
            return f"{self.name} {b} bytes left"
        if t == 14:
            return f"usb_irq_polled INTUSB 0x{a:02X} INTTX 0x{b >> 8:02X} INTRX 0x{b & 0xFF:02X}"
        if t == 15:
            return f"restart exception {a} ({RESTART_EXC_TYPES.get(a, '?')}) pc.lo16 0x{b:04X}"
        if t in (16, 19):
            return f"{self.name} {_usb_state_field(t, a, b)}"
        if t == 17:
            pkt = "TxPktRdy (packet waits, host takes no IN data)" if b & 1 else \
                ("UnderRun (host polls, nothing loaded)" if b & 4 else "idle")
            return f"tx_abort_txcsr POWER 0x{a:02X} TXCSR 0x{b:04X} {pkt}"
        if t == 18:
            return (f"tx_abort_frame tx_irq_enabled {a & 1} zlp_pending {(a >> 1) & 1} frame {b & 0x7FF}")
        return self.name


@dataclass
class EventLog:
    total: int
    now_ms: int
    events: list[DeviceEvent]
    #: fw 0.2.15 tail: a bus reset happened since the previous read; ``snapshot`` = the events right before it
    frozen: bool = False
    reset_ms: int = 0
    snapshot: list[DeviceEvent] = field(default_factory=list)
    has_snapshot: bool = False
    #: fw 0.2.24 tail: the last bus-level events (BUS_EVENT_TYPES), which EP0 setups and DTR edges cannot evict
    bus_events: list[DeviceEvent] = field(default_factory=list)
    bus_total: int = 0
    has_bus_ring: bool = False


def _event(payload: bytes, off: int) -> DeviceEvent:
    return DeviceEvent(*struct.unpack_from("<IBBH", payload, off))


def parse_event_log(payload: bytes) -> EventLog:
    """GET_EVENT_LOG: u16 n (<= 64, <= 128 from fw 0.2.15), u16 0, u32 total, u32 now_ms, then n x (u32 ms, u8 type,
    u8 a, u16 b), oldest first. fw 0.2.15 appends a reset snapshot: u16 m (<= 16), u16 frozen, u32 reset_ms, m
    entries (the events right before the first bus reset since the previous read). fw 0.2.24 then appends the
    bus-level ring: u16 k (<= 16), u16 0, u32 bus events since boot, k entries. Older firmware: exactly 12 + 8n."""
    if len(payload) < 12:
        return EventLog(0, 0, [])
    n, _r, total, now = struct.unpack_from("<HHII", payload)
    n = min(n, (len(payload) - 12) // 8)
    ev = [_event(payload, 12 + 8 * i) for i in range(n)]
    log = EventLog(total, now, ev)
    off = 12 + 8 * n
    if len(payload) >= off + 8:
        m, frozen, reset_ms = struct.unpack_from("<HHI", payload, off)
        m = min(m, (len(payload) - off - 8) // 8)
        log.has_snapshot = True
        log.frozen = bool(frozen)
        log.reset_ms = reset_ms
        log.snapshot = [_event(payload, off + 8 + 8 * i) for i in range(m)]
        off += 8 + 8 * m
        if len(payload) >= off + 8:
            k, _r, bus_total = struct.unpack_from("<HHI", payload, off)
            k = min(k, (len(payload) - off - 8) // 8)
            log.has_bus_ring = True
            log.bus_total = bus_total
            log.bus_events = [_event(payload, off + 8 + 8 * i) for i in range(k)]
    return log


def pack_event_log(log: EventLog) -> bytes:
    out = struct.pack("<HHII", len(log.events), 0, log.total & 0xFFFFFFFF, log.now_ms & 0xFFFFFFFF) + \
        b"".join(struct.pack("<IBBH", e.ms & 0xFFFFFFFF, e.type, e.a, e.b) for e in log.events)
    if log.has_snapshot:
        out += struct.pack("<HHI", len(log.snapshot), int(log.frozen), log.reset_ms & 0xFFFFFFFF) + \
            b"".join(struct.pack("<IBBH", e.ms & 0xFFFFFFFF, e.type, e.a, e.b) for e in log.snapshot)
        if log.has_bus_ring:
            out += struct.pack("<HHI", len(log.bus_events), 0, log.bus_total & 0xFFFFFFFF) + \
                b"".join(struct.pack("<IBBH", e.ms & 0xFFFFFFFF, e.type, e.a, e.b) for e in log.bus_events)
    return out


def link_stats_names(n: int) -> tuple[str, ...]:
    """Counter names for a GET_LINK_STATS answer with ``n`` counters (the layout depends on the firmware)."""
    return LINK_STATS_NAMES_35 if n >= 35 else LINK_STATS_NAMES


def parse_link_stats(payload: bytes) -> dict[str, int]:
    """u16 n, u16 0, then n x u32 (counter i at byte 4 + 4i; word 0 is the count, not a counter). Names follow the
    layout of the firmware that sent ``n`` counters; unknown extra counters are named ``c<index>``."""
    if len(payload) < 4:
        return {}
    (n,) = struct.unpack_from("<H", payload)
    n = min(n, (len(payload) - 4) // 4)
    vals = struct.unpack_from(f"<{n}I", payload, 4)
    names = link_stats_names(n)
    return {(names[i] if i < len(names) else f"c{i}"): v for i, v in enumerate(vals)}


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
    Cmd.GET_CAPTURE_INFO: 0, Cmd.GET_EVENT_LOG: 0, Cmd.SD_INFO: 4, Cmd.SD_READ_SECTORS: 8, Cmd.SD_STAGE: 8, Cmd.SD_WRITE_SECTORS: 16,
    Cmd.READ_SPECTRUM: ReadSpectrumReq.size(), Cmd.GET_DECIM: 0, Cmd.SET_DECIM: SetDecimReq.size(),
    Cmd.GET_HEALTH: 0, Cmd.SAVE_REPORT: 0,
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
