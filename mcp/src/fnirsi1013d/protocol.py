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
from typing import Any, ClassVar, Iterable, Iterator

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
#: (fw 0.2.49): USB suspend watch, link counters 60-61, event type 20; 1.15 (fw 0.2.50): calibration v4; 1.16
#: (fw 0.2.52): diagnostics readers; 1.17 (fw 0.2.58, FPGA 0x1536, row 56): sequencer (segmented capture, hardware
#: averaging: caps SEQ, SET_SEQ / GET_SEQ / READ_SEQ, READ_RAW addr_mode 3), the features word and UI Average state in
#: GET_STATUS 44..47 (48 bytes) and GET_DECIM 32..35, GET_CAPTURE_INFO 96 bytes; 1.18 (fw 0.2.61, row 57): full-rate
#: capture check, link counters 62-63, event types 21 / 22; 1.19 (fw 0.2.62, FPGA 0x1538, row 58): every full-rate
#: capture checked, caps ENC_CHECK, features bit 5, GET_HEALTH 104 bytes; 1.20 (fw 0.2.66, row 60): the on-device Bode
#: plot (BODE_CTRL / READ_BODE, display_mode 5, features bit 6); 1.21 (fw 0.2.67, row 61, provisional numbering):
#: CAL_PROBE, the calibration wizard's zero measurement from the host; 1.22 (fw 0.2.72, row 62): the FPGA bus contract,
#: link counters 64-67; 1.23 (fw 0.2.78, row 63): requests close the modal screens; 1.24 (fw 0.2.79, row 64,
#: provisional numbering): FPGA 0x153A capture stages (caps CAPTURE_STAGES, features bit 7, GET_HEALTH 124 bytes
#: layout 2, GET_CAL 364 bytes, event 23, EXT shifts 12 / 14 legacy samples); 1.25 (fw 0.2.102): one built-for
#: bitstream (FW_FPGA_VERSION, 0x153B then; GET_INFO 80 bytes with 76..79 fw_fpga_version / fpga_flags, GET_HEALTH 128 bytes
#: layout 3 with 124..127 the same; a mismatch answers E_UNSUPPORTED detail 1 to every command outside BASIC_CMDS), one
#: UI Average behaviour (GET_STATUS 46: 1 = the MCU's mean of N records, 2 = a sequencer run); 1.26 (FPGA 0x153C,
#: docs/plans/fpga-0x153c.md, fw 0.2.104): trigger types (SET_TRIGGER2 0x0219, GET_TRIGGER 52 bytes, caps
#: TRIG_TYPES), the trigger hold-off (caps HOLDOFF: 0x1B in 5 ns units), peak detect (SET_DECIM filter 4, READ_RAW
#: layout 3, GET_CAPTURE_INFO 79 bit 1, caps PEAK_DETECT) and the hardware counter (GET_COUNTER 0x0213, caps COUNTER);
#: 1.27 (FPGA 0x153D, fw 0.2.105): the streaming logger (LOG_START / LOG_READ / LOG_STOP 0x0214-0x0216,
#: acq_state 5 LOGGING, caps LOGGER), the generator as a fractional two-step square (SET_GEN raw modes P16 / H16 on
#: the 5 ns clock, GET_GEN 40 bytes; the DDS is gone, GET_INFO gen_clock_hz 200 MHz), READ_BODE rows carry the
#: stimulus period P in 5 ns clocks. The pattern generator of the 1.27 drafts was cut (owner 2026-10-21, fw-nopat):
#: 0x0217 / 0x021A are unknown commands, caps bits 24 / 27 and features bits 12 / 13 reserved, trigger source 3 gone
PROTO_MINOR = 27
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
    ACQUIRE_AVG = 0x0207
    SET_ACQ_STATE = 0x0208  # protocol 1.3, SHARED_MODE
    GET_CAPTURE_INFO = 0x0209  # protocol 1.3, SHARED_MODE
    READ_SPECTRUM = 0x020A  # protocol 1.7, fw 0.2.28: the spectrum the scope's spectrum view shows (main loop)
    GET_DECIM = 0x020C  # protocol 1.9, fw 0.2.30, cap DECIM: decimation filter setting and state
    SET_DECIM = 0x020D  # protocol 1.9: the decimation filter (the UI's acquisition Filter setting)
    SET_SEQ = 0x020E  # protocol 1.17, fw 0.2.58, cap SEQ: sequencer setting for host ARM (segments / averaging)
    GET_SEQ = 0x020F  # protocol 1.17: the setting, its fit with the next ARM, FPGA status, the latest run
    READ_SEQ = 0x0210  # protocol 1.17: segment table or averaged record of the latest run
    BODE_CTRL = 0x0211  # protocol 1.20, fw 0.2.66, cap DECIM: start / stop the on-device Bode sweep, the view, thru
    READ_BODE = 0x0212  # protocol 1.20: the Bode sweep's header and its 20-byte rows
    #: protocol 1.26 (FPGA 0x153C, cap COUNTER): the hardware counter's snapshot (read 0x35), or its configuration
    GET_COUNTER = 0x0213
    #: protocol 1.27 (FPGA 0x153D, cap LOGGER): the streaming logger (contract section 6)
    LOG_START = 0x0214
    LOG_READ = 0x0215
    LOG_STOP = 0x0216
    #: 0x0217 / 0x021A reserved (SET_PATTERN / SET_PATTERN_TRIGGER of the 1.27 drafts, cut with 0x153D's patterns)
    #: protocol 1.26 (cap TRIG_TYPES): trigger type, polarity, N / H, hold-off: the 0x153C trigger qualifier
    SET_TRIGGER2 = 0x0219
    GET_GEN = 0x0300
    SET_GEN = 0x0301
    GET_CAL = 0x0400
    READ_SCREEN = 0x0401
    #: protocol 1.21 (row 61, provisional numbering; fw 0.2.67): the calibration wizard's zero measurement of one
    #: channel and range, nothing stored (no capability bit; older firmware answers E_UNKNOWN_CMD)
    CAL_PROBE = 0x0402
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
#: Commands answered while the UI is modal (LOCAL_BUSY, section 6.1). Before protocol 1.23 every other command got
#: E_BUSY then; since 1.23 (fw 0.2.78) the others close the scope's modal screens and run (YIELD_MINOR).
BUSY_ALLOWED = frozenset({Cmd.PING, Cmd.GET_INFO, Cmd.GET_STATUS, Cmd.GET_BATTERY, Cmd.GET_LINK_STATS,
                          Cmd.GET_EVENT_LOG, Cmd.GET_HEALTH})
#: Control requests allowed while a deferred request is pending (section 3.2).
#: SET_ACQ_STATE (1.3) is a control request too: RUN/STOP while a WAIT is pending aborts it (E_ABORTED).
CONTROL_CMDS = frozenset({Cmd.GET_STATUS, Cmd.STOP, Cmd.SET_REMOTE, Cmd.PING, Cmd.SET_ACQ_STATE})
#: Protocol 1.25 (fw 0.2.102): the commands a scope whose FPGA does not run the firmware's bitstream still answers
#: (the firmware's F_BASIC set: the link, the diagnostics readers, the card, the screen, FEL; nothing that configures
#: or reads the acquisition). Every other command gets E_UNSUPPORTED detail UNSUP_FPGA_MISMATCH with FPGA_MISMATCH_TEXT.
BASIC_CMDS = frozenset({Cmd.PING, Cmd.GET_INFO, Cmd.GET_STATUS, Cmd.GET_BATTERY, Cmd.TEST_PATTERN, Cmd.GET_LINK_STATS,
                        Cmd.GET_BAD_FRAME, Cmd.GET_EVENT_LOG, Cmd.GET_HEALTH, Cmd.READ_SPECTRUM, Cmd.SD_INFO,
                        Cmd.SD_READ_SECTORS, Cmd.SD_STAGE, Cmd.SD_WRITE_SECTORS, Cmd.SAVE_REPORT, Cmd.GET_CAL,
                        Cmd.READ_SCREEN, Cmd.FFT_BENCH, Cmd.ENTER_FEL})
#: fw 0.2.105 (fw-relay, owner 2026-10-22): the requests that set up or run the scope's acquisition (pc_cmds.c F_SCOPE).
#: With the Bode view open they close it first (its settings from before come back, as Menu > Scope), then apply;
#: SET_ACQ_STATE RUN closes it too (its handler). No Bode sweep starts from them; BODE_CTRL and the reads keep the view.
BODE_LEAVE_CMDS = frozenset({Cmd.SET_CHANNEL, Cmd.SET_TIMEBASE, Cmd.SET_TRIGGER, Cmd.SET_TRIGGER2, Cmd.LOG_START,
                             Cmd.ARM, Cmd.SET_DECIM, Cmd.SET_SEQ, Cmd.SET_GEN, Cmd.CAL_PROBE})
#: The FPGA bitstream the current firmware is built for (pc_cmds.h PC_FW_FPGA_VERSION; owner decision 2026-10-20: every
#: firmware supports the latest bitstream only). The live value is InfoResp.fw_fpga_version / HealthResp.fw_fpga_version;
#: this constant is what the simulator reports and what older-protocol answers (76 / 124 bytes, fields read as 0) lack.
FW_FPGA_VERSION = 0x153D
#: GET_INFO 78 / GET_HEALTH 126 bit 0: the version word the FPGA reported at boot differs from fw_fpga_version
FPGA_F_MISMATCH = 0x01
#: E_UNSUPPORTED detail of every non-basic command on such a scope, with this text
UNSUP_FPGA_MISMATCH = 1
FPGA_MISMATCH_TEXT = f"FPGA bitstream mismatch: this firmware needs 0x{FW_FPGA_VERSION:04X}"
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
    Status.E_UNSUPPORTED: "not available on this FPGA bitstream or firmware build (detail 1: the scope does not run "
                          "the bitstream the firmware is built for; flash it, docs/guide/prepare.md)",
    Status.E_NOT_REMOTE: "command needs REMOTE ownership; acquire the remote session first",
    Status.E_BUSY: "the scope cannot take the request now (see busy_hint for the reason)",
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
    #: a programmable square generator on the calibration pin; since 0x153D (protocol 1.27) the fractional two-step
    #: square on the 5 ns clock (SET_GEN raw modes P16 / H16), before it Atlan4's 50 MHz DDS (was named GENERATOR_DDS)
    GENERATOR = 1 << 1
    HOLDOFF = 1 << 2
    EXT_TRIGGER = 1 << 3
    TRIG_BOTH_CORES = 1 << 4
    FORCE_TRIGGER = 1 << 5
    SCREENSHOT = 1 << 6
    DEVICE_AVERAGE = 1 << 7
    DEBUG_RAW = 1 << 8
    # bit 9 reserved: was LIVE_READ (READ_LIVE, never built), retired in protocol 1.27
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
    #: protocol 1.17 (FPGA 0x1536+): sequencer, segmented capture and hardware averaging
    SEQ = 1 << 17
    #: protocol 1.19 (FPGA 0x1538+): the per-capture encode self-check, FPGA read 0x33
    ENC_CHECK = 1 << 18
    #: protocol 1.24 (FPGA 0x153A+): per-core ADC capture stage (0x63), outlier counters (0x36)
    CAPTURE_STAGES = 1 << 19
    #: protocol 1.26 (FPGA 0x153C): trigger types pulse width / Nth edge / timeout (0x5E, 0x5F, 0x60, read 0x34),
    #: SET_TRIGGER2; HOLDOFF (bit 2) is set with it: 0x1B is a trigger hold-off in 5 ns units
    TRIG_TYPES = 1 << 20
    #: protocol 1.26 (FPGA 0x153C): peak-detect acquisition (0x56 bit 1), SET_DECIM filter 4, READ_RAW layout 3
    PEAK_DETECT = 1 << 21
    #: protocol 1.26 (FPGA 0x153C): the hardware counter with min / max (0x61, 0x62, read 0x35), GET_COUNTER
    COUNTER = 1 << 22
    #: protocol 1.27 (FPGA 0x153D): the streaming logger (0x69, 0x6A, read 0x38), LOG_START / LOG_READ / LOG_STOP
    LOGGER = 1 << 23
    #: bit 24 reserved (PATTERN_GEN of the 1.27 drafts, cut)
    #: protocol 1.28 (FPGA 0x153E): runt and window types, the pulse-width range (SET_TRIGGER2 types 4 / 5, RANGE)
    TRIG_RUNT_WINDOW = 1 << 26
    #: bit 27 reserved (PATTERN_TRIGGER of the 1.27 drafts, cut)


class Features(enum.IntFlag):
    """GET_STATUS bytes 44-45 / GET_DECIM 32-33 features word (protocol 1.17): what the bitstream can do."""

    EXT_TRIGGER = 1 << 0  # 0x1533+
    DECIM = 1 << 1  # 0x1534+
    FIR = 1 << 2  # 0x1535+
    SEGMENTS = 1 << 3  # 0x1536+
    HW_AVERAGE = 1 << 4  # 0x1536+
    ENC_CHECK = 1 << 5  # 0x1538+ (protocol 1.19)
    BODE = 1 << 6  # protocol 1.20: what the on-device Bode plot needs (DECIM and GENERATOR: AL3 0x1534+)
    CAPTURE_STAGES = 1 << 7  # 0x153A+ (protocol 1.24): ADC capture stages and outlier counters
    #: 0x153C (protocol 1.26). The contract (docs/plans/fpga-0x153c.md 2.3) names bits 5-7, which rows 58 / 60 / 64 had
    #: taken; the next free bits are used
    TRIG_TYPES = 1 << 8
    PEAK = 1 << 9
    COUNTER = 1 << 10
    #: 0x153D (protocol 1.27)
    LOGGER = 1 << 11
    #: bits 12 / 13 reserved (PATTERN / PATTERN_TRIGGER of the 1.27 drafts, cut)


def feature_names(features: int) -> list[str]:
    return [f.name.lower() for f in Features if features & f and f.name]


#: GET_STATUS byte 46 (protocol 1.17): the acquisition menu's Average. Since fw 0.2.102 (1.25) one behaviour, the mean
#: of N records (byte 47 = log2 N; 0 = single records, two did not fit the 40 ms budget): 1 = summed by the MCU
#: (16-bit records and legacy records over 2046 words), 2 = one sequencer run (legacy records that fit the
#: accumulator). Before 0.2.102, 1 was a software running average.
UI_AVERAGE_NAMES = {0: "off", 1: "mcu", 2: "hardware"}


def caps_names(caps: int) -> list[str]:
    return [c.name for c in Caps if caps & c and c.name]


#: Retired command codes (section 8.1 "(reserved)" rows): the firmware answers them E_UNSUPPORTED, not E_UNKNOWN_CMD.
#: 0x0206 was READ_LIVE (live ADC bytes, never built), retired in protocol 1.27 with the FPGA's reads 0x24..0x27.
RETIRED_CMDS: frozenset[int] = frozenset({0x0206})


#: Capability bit each command needs (section 8.1, column Cap).
CMD_CAP: dict[Cmd, Caps] = {
    Cmd.FORCE: Caps.FORCE_TRIGGER,
    Cmd.ACQUIRE_AVG: Caps.DEVICE_AVERAGE,
    Cmd.SET_GEN: Caps.GENERATOR,
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
    Cmd.SET_SEQ: Caps.SEQ,
    Cmd.GET_SEQ: Caps.SEQ,
    Cmd.READ_SEQ: Caps.SEQ,
    Cmd.BODE_CTRL: Caps.DECIM,
    Cmd.READ_BODE: Caps.DECIM,
    Cmd.GET_COUNTER: Caps.COUNTER,
    Cmd.SET_TRIGGER2: Caps.TRIG_TYPES,
    Cmd.LOG_START: Caps.LOGGER,
    Cmd.LOG_READ: Caps.LOGGER,
    Cmd.LOG_STOP: Caps.LOGGER,
}


class FpgaVariant(enum.IntEnum):
    """GET_INFO byte 10 (wire encoding). Since fw 0.2.102 the firmware supports one bitstream (FW_FPGA_VERSION, an AL3)
    and always reports 2; the variant byte no longer encodes the version word (the stock / EF2 / EP4 values are history,
    kept for older answers)."""

    UNKNOWN = 0
    STOCK = 1
    AL3 = 2
    EF2 = 3
    EP4 = 4


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
    #: 1.20 (fw 0.2.66): the Bode plot (the scope's own acquisition is off; READ_BODE reads the table)
    BODE = 5


#: Protocol 1.23 (fw 0.2.78, docs/protocol.md section 14 row 63): a request that needs the scope's main loop while a menu,
#: fold-out, dialog or screen is open closes them by their cancel paths (silently since fw 0.2.81) and
#: is answered; E_BUSY only with one of the BusyDetail reasons. Older firmware answered E_BUSY detail 0 while any menu was
#: open, and the person had to close it.
YIELD_MINOR = 23


class BusyDetail(enum.IntEnum):
    """E_BUSY detail (protocol 1.23; before, 0 for a modal UI as well as a pending deferred request)."""

    UI = 0  #: a deferred request is pending; before 1.23 also "a menu is open on the scope" (text "UI busy")
    CAPTURE = 1  #: READ_RAW: the UI capture in flight did not finish within 1 s
    NO_YIELD = 2  #: the UI did not get back to its main screen within 1.5 s
    CAL = 3  #: a calibration measurement runs (wizard step, volts calibration step)
    SAVE = 4  #: a file save is in progress
    USB_DRIVE = 5  #: USB drive mode


#: The firmware's E_BUSY texts per detail (1.23)
BUSY_TEXTS: dict[int, str] = {
    BusyDetail.CAPTURE: "UI capture running",
    BusyDetail.NO_YIELD: "UI did not return to the main screen",
    BusyDetail.CAL: "calibration measurement running",
    BusyDetail.SAVE: "file save in progress",
    BusyDetail.USB_DRIVE: "USB drive mode",
}


def busy_hint(detail: int, text: str = "") -> str:
    """What an E_BUSY means and what to do, from its detail and text (protocol 1.23 details; older firmware's
    detail 0 "UI busy" = a menu open on the scope)."""
    if text.startswith("deferred"):
        return "a deferred request (WAIT) is already pending; wait for its answer"
    return {
        BusyDetail.CAPTURE: "the scope's own capture in flight did not finish within 1 s; retry",
        BusyDetail.NO_YIELD: "the scope's UI did not get back to its main screen within 1.5 s (auto set, a slow capture "
                             "or a step that has to finish); retry",
        BusyDetail.CAL: "the scope is measuring for its calibration; wait until the step ends (the person can press "
                        "Cancel on the scope), then retry",
        BusyDetail.SAVE: "the scope is writing a file to its SD card; retry in a moment",
        BusyDetail.USB_DRIVE: "the scope is in USB drive mode; the person leaves it with Back on the scope (Exit on "
                              "firmware before 0.2.105)",
    }.get(detail, "a menu or dialog is open on the scope (firmware before 0.2.78, protocol < 1.23): ask the person "
                  "to close it, then retry")


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
    #: protocol 1.27: the streaming logger runs (LOG_START until LOG_STOP / its duration)
    LOGGING = 5


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
#: the generator clock (GET_INFO gen_clock_hz): the square runs on the 200 MHz sample clock since 0x153D
GEN_CLOCK_HZ = 200_000_000
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
    """GET_INFO response (section 8.2): 76 bytes up to protocol 1.24, 80 since 1.25 (fw 0.2.102): 76 u16
    ``fw_fpga_version`` (the bitstream the firmware is built for), 78 u8 ``fpga_flags`` (FPGA_F_MISMATCH: the scope
    runs another word), 79 0. A 76-byte answer reads them as 0 (``fpga_mismatch`` False)."""

    FIELDS = [("proto_major", "B"), ("proto_minor", "B"), ("max_request_frame", "H"),
              ("max_response_payload", "I"), ("fpga_version", "H"), ("fpga_variant", "B"), ("adc_bits", "B"),
              ("ring_words", "H"), ("channels", "B"), ("cores_per_channel", "B"), ("base_clock_hz", "I"),
              ("caps", "I"), ("gen_clock_hz", "I"), ("max_total_samples", "H"), ("min_total_samples", "H"),
              ("read_lag", "B"), ("n_ranges", "B"), ("n_hw_ranges", "B"), ("interleave_order", "B"),
              ("fw_version", "16s"), ("fw_build", "I"), ("serial", "16s"), ("screen_width", "H"),
              ("screen_height", "H"), ("fw_fpga_version", "H"), ("fpga_flags", "B"), ("_r79", "x")]
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
    #: 1.25: the bitstream this firmware is built for (0 in a 76-byte answer) and the FPGA_F_* flags
    fw_fpga_version: int = 0
    fpga_flags: int = 0

    @property
    def fw_version_str(self) -> str:
        return _cstr(self.fw_version)

    @property
    def serial_str(self) -> str:
        return _cstr(self.serial)

    @property
    def fpga_mismatch(self) -> bool:
        """1.25: the FPGA reported another version word than the firmware is built for; every command outside
        BASIC_CMDS answers E_UNSUPPORTED detail UNSUP_FPGA_MISMATCH until that bitstream is flashed."""
        return bool(self.fpga_flags & FPGA_F_MISMATCH)


@dataclass
class StatusResp(Payload):
    """GET_STATUS response body: 28 bytes in protocol 1.0, 32 in 1.1 (pass_ms, pass_max_ms appended; a 1.0
    device leaves them 0), 44 in 1.3 (settings_seq, capture_settings_seq, run_state, capture_origin), 48 in 1.17
    (features, UI Average), 52 in 1.26 (the trigger qualifier's status: FPGA 0x37, 0x34 byte 0, the edges still to
    count; read while a host capture is in flight, zeroed at ARM, the last values kept after DONE). Also returned by
    SET_REMOTE, ARM, FORCE, STOP, WAIT and SET_ACQ_STATE."""

    FIELDS = [("control", "B"), ("acq_state", "B"), ("flags", "B"), ("ui_busy", "B"), ("capture_id", "I"),
              ("trigger_addr", "H"), ("capture_start_word", "H"), ("ms_since_arm", "I"), ("uptime_ms", "I"),
              ("watchdog_remaining_s", "H"), ("battery_level", "B"), ("exit_reason", "B"), ("reserved", "I"),
              ("pass_ms", "H"), ("pass_max_ms", "H"), ("settings_seq", "I"), ("capture_settings_seq", "I"),
              ("run_state", "B"), ("capture_origin", "B"), ("ext_flags", "B"), ("display_mode", "B"),
              ("features", "H"), ("ui_average", "B"), ("ui_average_log2n", "B"),
              ("fpga_37", "B"), ("fpga_34", "B"), ("edges_left", "H")]
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
    #: 1.17: Features of the bitstream (0 before 1.17)
    features: int = 0
    #: 1.17: the acquisition menu's Average (UI_AVERAGE_NAMES): 0 off; since 1.25 the mean of N records summed by
    #: 1 the MCU or 2 the sequencer (before 0.2.102: 1 was a software running average)
    ui_average: int = 0
    #: 1.17: log2 of the records N per displayed record (ui_average 1 or 2; 0 = single records)
    ui_average_log2n: int = 0
    #: 1.26: FPGA read 0x37 = { 0, 0, 0, hold-off running (0x10), type bit 2, 0, 0, 0 }
    fpga_37: int = 0
    #: 1.26: FPGA read 0x34 byte 0 = { 0, 0, counting (0x20), pulse began (0x10), EXT (0x08), longer (0x04), type[1:0] }
    fpga_34: int = 0
    #: 1.26: Nth edge: edges still to count (0x34 bytes 1..2)
    edges_left: int = 0

    @property
    def shared(self) -> bool:
        return self.control == Control.SHARED

    @property
    def holdoff_running(self) -> bool:
        return bool(self.fpga_37 & STATUS37_HOLDOFF_RUNNING)

    def trigger_status(self) -> dict[str, Any]:
        """1.26: the qualifier's status words in words."""
        t = (self.fpga_34 & STATUS34_TYPE_MASK) | (4 if self.fpga_37 & STATUS37_TYPE2 else 0)
        return {"type": TRIG_TYPE_NAMES[t] if t < len(TRIG_TYPE_NAMES) else t,
                "holdoff_running": self.holdoff_running, "counting": bool(self.fpga_34 & STATUS34_COUNTING),
                "pulse_began": bool(self.fpga_34 & STATUS34_PULSE_BEGAN), "ext": bool(self.fpga_34 & STATUS34_EXT),
                "longer": bool(self.fpga_34 & STATUS34_LONGER), "edges_left": self.edges_left}


#: GET_STATUS 48 (FPGA 0x37) and 49 (FPGA 0x34 byte 0) bits, protocol 1.26
STATUS37_HOLDOFF_RUNNING = 0x10
STATUS37_TYPE2 = 0x08
STATUS34_COUNTING = 0x20
STATUS34_PULSE_BEGAN = 0x10
STATUS34_EXT = 0x08
STATUS34_LONGER = 0x04
STATUS34_TYPE_MASK = 0x03


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
    #: fw 0.2.97..0.2.102 (protocol 1.24, spare bit): the record may be joined from two passes of the ring (a stop the
    #: firmware forced on FPGA 0x1532..0x153A / EF2 / EP4). Never set since fw 0.2.102 (1.25: the firmware supports
    #: 0x153B only, whose forced stop keeps the record whole); decoded for captures from older firmware
    F_MAYBE_SPLICED: ClassVar[int] = 0x40


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
class CaptureTrig2(Payload):
    """GET_CAPTURE_INFO bytes 96..111 (protocol 1.26): the trigger extension the capture was armed with: 96 u8 type,
    97 u8 flags, 98 u16 0, 100 u32 N, 104 u32 H, 108 u32 hold-off in 5 ns units (the polarity is byte 22, the level
    byte 23)."""

    FIELDS = [("type", "B"), ("flags", "B"), ("_r", "2x"), ("n", "I"), ("h", "I"), ("holdoff", "I")]
    type: int = 0  # TRIG_EDGE (defined below)
    flags: int = 0
    n: int = 0
    h: int = 0
    holdoff: int = 0

    def trigger2(self, polarity: int, source: int, level: int) -> "Trigger2":
        return Trigger2(self.type, polarity, self.flags, source, self.n, self.h, level, 0, self.holdoff, 0)


@dataclass
class CaptureInfo:
    """Decoded GET_CAPTURE_INFO (68 bytes; 76 from protocol 1.9 with the capture's decimation filter; 96 from 1.17
    with the capture's sequencer run, ``run``; 112 from 1.26 with the trigger extension ``trig2``)."""

    header: CaptureInfoHeader
    channels: list[CaptureChannelInfo]
    #: 1.9: the capture's mode (FPGA 0x29 byte 0 layout, 0 = legacy), S, EXT latency shift (samples), N
    decim_mode: int = 0
    decim_shift: int = 0
    ext_shift: int = 0
    decim_n: int = 0
    has_decim: bool = False
    #: 1.17: the capture's sequencer run (bytes 76..95; ``run.mode`` 0 = a single capture), None before 1.17
    run: "SeqRunInfo | None" = None
    #: 1.26: the trigger type, flags, N, H and hold-off as armed (bytes 96..111), None before 1.26
    trig2: "CaptureTrig2 | None" = None

    SIZE: ClassVar[int] = 36 + 2 * 16

    def pack(self) -> bytes:
        out = self.header.pack() + b"".join(c.pack() for c in self.channels)
        if self.has_decim or self.run is not None or self.trig2 is not None:
            out += struct.pack("<BBHI", self.decim_mode, self.decim_shift, self.ext_shift, self.decim_n)
        if self.run is not None or self.trig2 is not None:
            out += (self.run or SeqRunInfo()).pack()
        if self.trig2 is not None:
            out += self.trig2.pack()
        return out

    @classmethod
    def unpack(cls, data: bytes) -> "CaptureInfo":
        h = CaptureInfoHeader.unpack(data)
        chans = [CaptureChannelInfo.unpack(data[36 + 16 * i: 52 + 16 * i]) for i in range(2)]
        ci = cls(h, chans)
        if len(data) >= 76:
            ci.decim_mode, ci.decim_shift, ci.ext_shift, ci.decim_n = struct.unpack_from("<BBHI", data, 68)
            ci.has_decim = True
        if len(data) >= 96:
            ci.run = SeqRunInfo.unpack(data[76:96])
        if len(data) >= 112:
            ci.trig2 = CaptureTrig2.unpack(data[96:112])
        return ci

    @property
    def is_run(self) -> bool:
        """1.17: the capture is a sequencer run (segments or hardware averaging)."""
        return self.run is not None and self.run.mode != SEQ_OFF

    @property
    def peak(self) -> bool:
        """1.26: a peak-detect record (byte 79 bit 1 CAPINFO_PEAK, or the shift byte's peak bit)."""
        return bool(self.run is not None and self.run.wide & CAPINFO_PEAK) or \
            bool(self.decim_mode & DECIM_WIDE and self.decim_shift & DECIM_PEAK_ACT)

    @property
    def decim(self) -> "DecimConfig | None":
        """The capture's decimation (None for a legacy 8-bit capture; ``peak`` set for a 1.26 peak record)."""
        if not self.decim_mode & DECIM_WIDE:
            return None
        shift = self.decim_shift | (DECIM_PEAK_ACT if self.peak else 0)
        return DecimConfig.from_status(self.decim_mode, shift, self.decim_n)

    def channel_config(self, ch: int) -> "ChannelConfig":
        """The capture-time channel settings as a CHANNEL_CONFIG (dcoffset / center_cal are not carried)."""
        c = self.channels[ch]
        return ChannelConfig(channel=ch, enable=c.enable, range=c.range, hw_range=min(c.range, 5),
                             coupling=c.coupling, invert=c.invert, probe_index=c.probe_index, offset_pwm=c.offset_pwm,
                             uv_per_lsb_q8=c.uv_per_lsb_q8, zero_code_q8=c.zero_code_q8)

    def trigger(self) -> "Trigger":
        h = self.header
        per = 1 if self.decim is not None else 2  # 1.9: one 16-bit sample per ring word
        t2 = self.trig2.trigger2(h.trig_edge, h.trig_source, h.trig_level) if self.trig2 is not None else None
        return Trigger(source=h.trig_source, edge=h.trig_edge, level=h.trig_level, mode=h.trig_mode,
                       pretrigger_samples=h.pretrigger_samples, total_samples=h.total_samples,
                       holdoff=self.trig2.holdoff if self.trig2 is not None else 0,
                       pretrigger_words=h.pretrigger_samples // per, total_words=h.total_samples // per, trigger2=t2)

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
        if self.trig2 is not None:  # 1.26: part of the fingerprint
            d["trig2"] = f"type {self.trig2.type} flags {self.trig2.flags} N {self.trig2.n} H {self.trig2.h} " \
                         f"holdoff {self.trig2.holdoff}"
        for i, c in enumerate(self.channels):
            for k in ("enable", "range", "coupling", "invert", "probe_index", "offset_pwm"):
                d[f"CH{i + 1}.{k}"] = getattr(c, k)
        return d


# Sequencer (protocol 1.17, fw 0.2.58, FPGA 0x1536, section 14 row 56): segmented capture and hardware averaging.
# The maths mirror firmware/scope/pc_seq.c and are checked against the "seq" section of docs/protocol-vectors.json.
# A "word" is a memory word as for 0x0B / 0x0C: one A/B pair per channel (legacy), one 16-bit sample with the filter.

SEQ_OFF, SEQ_SEGMENTS, SEQ_AVERAGE = 0, 1, 2
SEQ_MODE_NAMES = {SEQ_OFF: "off", SEQ_SEGMENTS: "segments", SEQ_AVERAGE: "averaging"}
SEQ_F_CIRCULAR = 0x01  # SET_SEQ flags bit 0 (segments only)
SEQ_F_VALID = 0x01
SEQ_M_MIN, SEQ_M_MAX = 5, 12
SEQ_WORDS = 8192  # sample memory
SEQ_ACC_WORDS = 2048  # accumulator (= segment table)
SEQ_TAIL = 2  # words written after a record window: total <= 2^m - 2
SEQ_AVG_MAX_TOTAL = 2046
SEQ_LEGACY_MAX_N = 257  # legacy 16-bit lane sums are exact up to 255 x 257 = 65535
SEQ_MAX_N = 65536
#: GET_CAPTURE_INFO 77 / GET_SEQ 47 run flags
SEQ_RUN_CIRCULAR, SEQ_RUN_STOPPED, SEQ_RUN_WRAPPED = 0x01, 0x02, 0x04
#: GET_CAPTURE_INFO byte 79 (SeqRunInfo.wide): bit 0 16-bit records, bit 1 (1.26) a peak-detect record
CAPINFO_WIDE, CAPINFO_PEAK = 0x01, 0x02
#: FPGA 0x2A byte 0 (GET_SEQ byte 24)
SEQ_ST_DONE, SEQ_ST_OVERFLOW, SEQ_ST_RUNNING, SEQ_ST_FMT16, SEQ_ST_RAW, SEQ_ST_CIRCULAR = 0x80, 0x40, 0x20, 0x10, 8, 4
#: READ_SEQ what
SEQ_READ_TABLE, SEQ_READ_AVERAGE = 0, 1
#: READ_SEQ S value for the plain sums
SEQ_SHIFT_SUMS = 0xFF
#: READ_SEQ header format bits
SEQ_FMT_WIDE, SEQ_FMT_U32 = 0x01, 0x02
#: GET_SEQ byte 16 / SET_SEQ refusals: why a setting does not fit the next arm's geometry
SEQ_FIT_OK, SEQ_FIT_MODE, SEQ_FIT_FLAGS, SEQ_FIT_M, SEQ_FIT_COUNT, SEQ_FIT_TOTAL, SEQ_FIT_PRE, SEQ_FIT_N = range(8)
#: 1.26: SET_SEQ mode 2 (averaging) while peak detect is on: E_STATE detail 7 (the sums would mix max and min)
SEQ_FIT_PEAK = 7
SEQ_FIT_NAMES = {
    SEQ_FIT_OK: "fits",
    SEQ_FIT_MODE: "unknown mode",
    SEQ_FIT_FLAGS: "unknown flags, or circular outside segment mode",
    SEQ_FIT_M: "segment slot exponent m outside 5..12",
    SEQ_FIT_COUNT: "count 0, more segments than 8192 >> m, or positions x total words > 2048",
    SEQ_FIT_TOTAL: "record too long: segments need total words <= 2^m - 2, averaging total words <= 2046",
    SEQ_FIT_PRE: "pretrigger must be shorter than the record",
    SEQ_FIT_N: "records per position N outside 1..65536 (at most 257 with 8-bit records)",
    # SEQ_FIT_PEAK shares the code 7 as an E_STATE detail (SET_SEQ only; GET_SEQ's fit byte never reads it)
}
SEQ_PEAK_TEXT = "peak detect: averaging is refused (the sums would mix max and min); set the filter first"
#: UI averaging (pc_seq_ui_plan_n, pc_seq_ui_plan, pc_seq_ui_plan_mcu): N records plus their dead time (and the MCU
#: path's readout, 4 bus bytes per ring word at about 300 ns each) fit the budget
SEQ_UI_BUDGET_US, SEQ_UI_MAX_N, SEQ_UI_MIN_N = 40000, 64, 2
SEQ_UI_READ_NS, SEQ_UI_READ_BYTES = 300, 4


@dataclass
class SetSeqReq(Payload):
    """SET_SEQ request, 16 bytes (protocol 1.17). ``n`` 65536 is sent as 0."""

    FIELDS = [("mode", "B"), ("flags", "B"), ("m", "B"), ("_r0", "x"), ("count", "H"), ("n_raw", "H"),
              ("holdoff_ns", "I"), ("_r1", "4x")]
    mode: int = 0
    flags: int = 0
    m: int = 0
    count: int = 0
    n_raw: int = 0
    holdoff_ns: int = 0

    @classmethod
    def make(cls, mode: int, flags: int = 0, m: int = 0, count: int = 0, n: int = 0,
             holdoff_ns: int = 0) -> "SetSeqReq":
        return cls(mode, flags, m, count, n & 0xFFFF, holdoff_ns)

    @property
    def n(self) -> int:
        return self.n_raw or (SEQ_MAX_N if self.mode == SEQ_AVERAGE else 0)


@dataclass
class SeqRunInfo(Payload):
    """GET_CAPTURE_INFO bytes 76..95 (protocol 1.17): the capture's sequencer run."""

    FIELDS = [("mode", "B"), ("flags", "B"), ("m", "B"), ("wide", "B"), ("count", "I"), ("untriggered", "H"),
              ("requested", "H"), ("n", "I"), ("holdoff_ns", "I")]
    #: 0 = a single capture, SEQ_SEGMENTS, SEQ_AVERAGE
    mode: int = 0
    #: SEQ_RUN_CIRCULAR / STOPPED / WRAPPED
    flags: int = 0
    m: int = 0
    #: bit 0 (CAPINFO_WIDE) 16-bit records; bit 1 (CAPINFO_PEAK, 1.26) peak-detect records; also set for single captures
    #: since 1.26 (0 before: the byte belonged to the run)
    wide: int = 0
    #: segments completed / records accumulated at DONE (a circular run counts beyond the slots)
    count: int = 0
    untriggered: int = 0
    #: segments / positions requested
    requested: int = 0
    n: int = 0
    holdoff_ns: int = 0

    @property
    def slots(self) -> int:
        """Table slots that may hold entries."""
        if self.mode != SEQ_SEGMENTS:
            return 0
        return SEQ_WORDS >> self.m if self.flags & SEQ_RUN_CIRCULAR else self.requested


@dataclass
class SeqState(Payload):
    """GET_SEQ response (and SET_SEQ's), 48 bytes (protocol 1.17)."""

    FIELDS = [("mode", "B"), ("flags", "B"), ("m", "B"), ("_r0", "x"), ("count", "H"), ("n_raw", "H"),
              ("holdoff_ns", "I"), ("holdoff_units", "I"), ("fit", "B"), ("wide", "B"), ("pre_words", "H"),
              ("total_words", "H"), ("max_count", "H"), ("fpga_status", "B"), ("seq_state", "B"),
              ("next_index", "H"), ("fpga_count", "I"), ("fpga_untriggered", "H"), ("held", "B"), ("_r1", "x"),
              ("run_capture_id", "I"), ("run_count", "I"), ("run_untriggered", "H"), ("run_mode", "B"),
              ("run_flags", "B")]
    #: the setting (applies to host ARM only, not saved)
    mode: int = 0
    flags: int = 0
    m: int = 0
    count: int = 0
    n_raw: int = 0
    holdoff_ns: int = 0
    #: hold-off in 5 ns units as written to 0x5C
    holdoff_units: int = 0
    #: SEQ_FIT_* of the setting with the next ARM's geometry
    fit: int = 0
    #: 1 = the next ARM records 16-bit samples
    wide: int = 0
    #: the next ARM's pretrigger / total words
    pre_words: int = 0
    total_words: int = 0
    #: largest count for the mode (8192 >> m segments, 2048 / total positions)
    max_count: int = 0
    #: FPGA 0x2A byte 0 now (SEQ_ST_*, bits 1..0 mode of the run)
    fpga_status: int = 0
    seq_state: int = 0
    next_index: int = 0
    fpga_count: int = 0
    fpga_untriggered: int = 0
    #: 1 = the FPGA still holds the latest capture's run (no arm since)
    held: int = 0
    #: the latest capture if it is a run (else 0), its count at DONE, untriggered, mode and flags
    run_capture_id: int = 0
    run_count: int = 0
    run_untriggered: int = 0
    run_mode: int = 0
    run_flags: int = 0

    @property
    def n(self) -> int:
        return self.n_raw or (SEQ_MAX_N if self.mode == SEQ_AVERAGE else 0)

    @property
    def fit_text(self) -> str:
        return SEQ_FIT_NAMES.get(self.fit, f"fit code {self.fit}")


@dataclass
class ReadSeqReq(Payload):
    """READ_SEQ request, 12 bytes (protocol 1.17): what (SEQ_READ_TABLE / SEQ_READ_AVERAGE), channel mask and S
    (averaged record), first entry or word, count."""

    FIELDS = [("what", "B"), ("ch_mask", "B"), ("shift", "B"), ("_r0", "x"), ("first", "H"), ("count", "H"),
              ("_r1", "4x")]
    what: int = 0
    ch_mask: int = 0
    shift: int = 0
    first: int = 0
    count: int = 0


@dataclass
class ReadSeqHeader(Payload):
    """READ_SEQ response header, 16 bytes."""

    FIELDS = [("capture_id", "I"), ("first", "H"), ("count", "H"), ("what", "B"), ("format", "B"),
              ("m_or_shift", "B"), ("ch_mask", "B"), ("pre_words", "H"), ("total_words", "H")]
    capture_id: int = 0
    first: int = 0
    count: int = 0
    what: int = 0
    #: SEQ_FMT_WIDE (16-bit records), SEQ_FMT_U32 (u32 sums)
    format: int = 0
    #: what 0: m; what 1: the S applied (0xFF for 16-bit sums; legacy 0xFF reads as S 0)
    m_or_shift: int = 0
    ch_mask: int = 0
    pre_words: int = 0
    total_words: int = 0


@dataclass(frozen=True)
class SeqEntry:
    """A segment table entry: timestamp in 5 ns units since the release of 0x01 (40 bits), trigger offset in the
    slot (trigger_address mod 2^m), triggered (0: ended on the auto time-out), valid (0: never written, or the slot a
    circular run was refilling)."""

    timestamp: int
    offset: int
    triggered: bool
    valid: bool

    @classmethod
    def decode(cls, e: int) -> "SeqEntry":
        return cls(e & 0xFFFFFFFFFF, (e >> 40) & 0x1FFF, bool((e >> 53) & 1), bool(e >> 63))

    def encode(self) -> int:
        return ((self.timestamp & 0xFFFFFFFFFF) | ((self.offset & 0x1FFF) << 40) | (int(self.triggered) << 53) |
                (int(self.valid) << 63))

    @property
    def seconds(self) -> float:
        return self.timestamp * 5e-9


def seq_max_count(mode: int, m: int, total_words: int) -> int:
    """pc_seq_max_count: 8192 >> m segments, or 2048 / total positions (0 when out of range)."""
    if mode == SEQ_SEGMENTS:
        return SEQ_WORDS >> m if SEQ_M_MIN <= m <= SEQ_M_MAX else 0
    if mode == SEQ_AVERAGE:
        return 0 if total_words == 0 or total_words > SEQ_AVG_MAX_TOTAL else SEQ_ACC_WORDS // total_words
    return 0


def seq_fit(mode: int, flags: int, m: int, count: int, n: int, pre_words: int, total_words: int,
            wide: bool) -> int:
    """pc_seq_fit: SEQ_FIT_* of a setting against the next arm's geometry (words) and record format."""
    if mode > SEQ_AVERAGE:
        return SEQ_FIT_MODE
    if flags & ~SEQ_F_VALID or (flags & SEQ_F_CIRCULAR and mode != SEQ_SEGMENTS):
        return SEQ_FIT_FLAGS
    if mode == SEQ_OFF:
        return SEQ_FIT_OK
    if mode == SEQ_SEGMENTS:
        if not SEQ_M_MIN <= m <= SEQ_M_MAX:
            return SEQ_FIT_M
        if count == 0 or count > SEQ_WORDS >> m:
            return SEQ_FIT_COUNT
        if total_words + SEQ_TAIL > 1 << m:
            return SEQ_FIT_TOTAL
    else:
        if n == 0 or n > SEQ_MAX_N or (not wide and n > SEQ_LEGACY_MAX_N):
            return SEQ_FIT_N
        if total_words > SEQ_AVG_MAX_TOTAL:
            return SEQ_FIT_TOTAL
        if count == 0 or count * total_words > SEQ_ACC_WORDS:
            return SEQ_FIT_COUNT
    if total_words == 0 or pre_words >= total_words:
        return SEQ_FIT_PRE
    return SEQ_FIT_OK


def seq_holdoff_units(ns: int) -> int:
    """Hold-off in 5 ns units, rounded up."""
    return (ns + 4) // 5


def seq_cmd57(mode: int, flags: int, raw: bool = False) -> int:
    return (mode & 3) | (0x04 if mode == SEQ_SEGMENTS and flags & SEQ_F_CIRCULAR else 0) | (0x08 if raw else 0)


def seq_slots(mode: int, flags: int, m: int, count: int) -> int:
    """Table slots that may hold entries: count, or 8192 >> m for a circular run."""
    if mode != SEQ_SEGMENTS:
        return 0
    return SEQ_WORDS >> m if flags & SEQ_F_CIRCULAR else count


def seq_seg_word(m: int, s: int, offset: int, pre_words: int, j: int) -> int:
    """Segment s: sample-memory word of record index j, from the table offset and the pretrigger words."""
    return (s << m) + ((offset - pre_words + j) & ((1 << m) - 1))


def seq_dead_ns(total_words: int, pre_words: int, word_ns: int) -> int:
    """Dead time per averaged record: post-pass (total x 5 + 40 ns) + restart (2 words) + pretrigger fill + 100 ns."""
    return total_words * 5 + 40 + (2 + pre_words) * word_ns + 100


def seq_ui_plan_n(per_record_ns: int) -> tuple[int, int]:
    """pc_seq_ui_plan_n (fw 0.2.102, one rule for both UI Average paths): the largest power of two N in 2..64 with
    N x per_record_ns <= the 40 ms budget, and N x per_record_ns in us rounded up; (0, 0) when two do not fit (the
    UI then shows single records, GET_STATUS 47 = 0)."""
    if per_record_ns <= 0:
        return 0, 0
    n = SEQ_UI_MAX_N
    while n >= SEQ_UI_MIN_N and n * per_record_ns > SEQ_UI_BUDGET_US * 1000:
        n >>= 1
    if n < SEQ_UI_MIN_N:
        return 0, 0
    return n, (n * per_record_ns + 999) // 1000


def seq_ui_plan(total_words: int, pre_words: int, divider: int) -> tuple[int, int]:
    """pc_seq_ui_plan, the hardware path (GET_STATUS 46 = 2): a legacy record (one word = 10 (D + 1) ns) costed at the
    record plus the sequencer's dead time; (0, 0) also when it does not fit the accumulator (> 2046 words) or
    pre >= total."""
    word_ns = 10 * (divider + 1)
    if total_words == 0 or total_words > SEQ_AVG_MAX_TOTAL or pre_words >= total_words or word_ns > 1_000_000:
        return 0, 0
    return seq_ui_plan_n(total_words * word_ns + seq_dead_ns(total_words, pre_words, word_ns))


def seq_ui_plan_mcu(total_words: int, pre_words: int, divider: int, wide: bool) -> tuple[int, int]:
    """pc_seq_ui_plan_mcu, the MCU path (GET_STATUS 46 = 1, fw 0.2.102): any record (``wide``: the 16-bit layout, one
    word = 5 (D + 1) ns, else 10 (D + 1)) costed at the record, the dead time and its readout (SEQ_UI_READ_BYTES bus
    bytes per word at SEQ_UI_READ_NS); (0, 0) when pre >= total or two do not fit."""
    word_ns = (5 if wide else 10) * (divider + 1)
    if total_words == 0 or pre_words >= total_words or word_ns > 1_000_000:
        return 0, 0
    return seq_ui_plan_n(total_words * word_ns + seq_dead_ns(total_words, pre_words, word_ns) +
                         total_words * SEQ_UI_READ_BYTES * SEQ_UI_READ_NS)


def ui_average_plan(total_words: int, pre_words: int, divider: int, wide: bool) -> tuple[int, int]:
    """What GET_STATUS 46 / 47 report with Average on (fw 0.2.102, pc_seq_hw.c): the hardware path for a legacy record
    that fits the accumulator, the MCU path for every other record; (state, log2 N) with state 2 hardware, 1 MCU (log2 N
    0 when two records do not fit the budget: single records)."""
    hw = not wide and total_words <= SEQ_AVG_MAX_TOTAL
    n, _us = seq_ui_plan(total_words, pre_words, divider) if hw else seq_ui_plan_mcu(total_words, pre_words, divider,
                                                                                      wide)
    if not n:
        return 1, 0
    return (2 if hw else 1), n.bit_length() - 1


def seq_avg_code(lane_sum: int, c: int, gain: int, off: int) -> int:
    """pc_seq_avg_code: corrected 8-bit code of an averaged legacy lane (sum of c raw core codes) through the core's
    Q16 correction (code = (raw x gain + off) / 65536), from the mean in Q8, rounded, clamped 0..255."""
    m8 = (lane_sum * 256 + c // 2) // c
    v = (m8 * gain + (off << 8) + (1 << 23)) >> 24
    return max(0, min(255, v))


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
#: the firmware writes the card only below the first partition and below this sector (PC_SD_LIMIT; 2048 up to fw
#: 0.2.100, 16384 = 8 MiB from 0.2.101); SD_INFO check 3 means the image ends past it. Cards for this firmware have
#: their FAT32 partition at this sector or later (firmware/README.md section 3)
SD_WINDOW_LIMIT = 16384


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


#: SET_TRIGGER2 / GET_TRIGGER 28 byte 0 (FPGA 0x5E bits 2..0): the trigger type
TRIG_EDGE, TRIG_PULSE_WIDTH, TRIG_NTH_EDGE, TRIG_TIMEOUT, TRIG_RUNT, TRIG_WINDOW = 0, 1, 2, 3, 4, 5
TRIG_TYPE_NAMES = ("edge", "pulse_width", "nth_edge", "timeout", "runt", "window")
#: trigger source (FPGA 0x15; SET_TRIGGER / SET_TRIGGER2 / GET_TRIGGER)
TRIG_SOURCE_CH1, TRIG_SOURCE_CH2, TRIG_SOURCE_EXT = 0, 1, 2
TRIG_SOURCE_NAMES = ("CH1", "CH2", "EXT")
#: polarity (0x16) names per type: index 0 = polarity 0, 1 = polarity 1
TRIG_POLARITY_NAMES = {
    TRIG_EDGE: ("rising", "falling"),
    TRIG_PULSE_WIDTH: ("positive", "negative"),  # the pulse; the trigger is its trailing edge
    TRIG_NTH_EDGE: ("rising", "falling"),  # the counted edge
    TRIG_TIMEOUT: ("high", "low"),  # the level that is held
    TRIG_RUNT: ("positive", "negative"),
    TRIG_WINDOW: ("leaving", "entering"),
}
#: SET_TRIGGER2 flags (byte 2)
TRIG2_F_LONGER = 0x01  # pulse width only: "longer than N" (0 = shorter than N); FPGA 0x5E bit 3
TRIG2_F_RANGE = 0x02  # 0x153E: pulse width inside [N, N2] (outside with LONGER); 0x5E bit 4. E_BAD_ARG on 1.26
TRIG2_F_BOTH_CORES = 0x04  # 0x153E: trigger on both ADC cores (legacy D = 0); 0x5E bit 5. E_BAD_ARG on 1.26
#: limits: N and H are u24 in the FPGA (0x5F / 0x60), the Nth-edge count 16 bits, the hold-off a u32 of 5 ns units
TRIG2_MAX_N = (1 << 24) - 1
TRIG2_MAX_NTH = 65535
TRIG2_MAX_HOLDOFF_UNITS = (1 << 32) - 1
TRIG2_HOLDOFF_UNIT_NS = 5
#: SET_TRIGGER2 E_BAD_ARG detail = the byte offset of the refused field (row 66)
TRIG2_D_TYPE, TRIG2_D_POLARITY, TRIG2_D_FLAGS, TRIG2_D_SOURCE, TRIG2_D_N, TRIG2_D_H, TRIG2_D_LEVEL, TRIG2_D_LEVEL2 = \
    0, 1, 2, 3, 4, 8, 12, 14
TRIG2_DETAIL_NAMES = {0: "type", 1: "polarity", 2: "flags", 3: "source", 4: "N", 8: "H", 12: "level", 14: "level2",
                      16: "holdoff", 20: "reserved"}


def holdoff_units(seconds: float) -> int:
    """A hold-off in seconds -> 0x1B units of 5 ns, rounded to the nearest, clamped to the u32."""
    return max(0, min(TRIG2_MAX_HOLDOFF_UNITS, int(round(seconds * 1e9 / TRIG2_HOLDOFF_UNIT_NS))))


@dataclass
class Trigger2(Payload):
    """SET_TRIGGER2 request (24 bytes, protocol 1.26, FPGA 0x153C, cap TRIG_TYPES, docs/protocol.md row 66), also
    GET_TRIGGER bytes 28..51 with the values in effect. Every field is written (no mask): a host reads GET_TRIGGER,
    changes fields and writes the block back; the geometry, mode and auto time-out stay SET_TRIGGER's.

    0 u8 type (TRIG_*; 4 runt / 5 window are 0x153E: E_BAD_ARG detail 0), 1 u8 polarity per type
    (TRIG_POLARITY_NAMES; = GET_TRIGGER ``edge``, FPGA 0x16), 2 u8 flags (TRIG2_F_LONGER, pulse width only; RANGE /
    BOTH_CORES and other bits: detail 2), 3 u8 source (0 CH1, 1 CH2, 2 EXT), 4 u32 n (pulse width / timeout: samples
    of the trigger stream 1..16777215; Nth edge: the count 1..65535; edge: 0; detail 4. The firmware converts nothing:
    a sample is one memory word on CH1 / CH2 (10 (D + 1) ns legacy, N x 5 ns with the filter or peak detect), one 5 ns
    clock on EXT), 8 u32 h (Nth edge: idle samples 0..16777215, 0 = count from the arm; other types 0; detail 8),
    12 u16 level (the raw 8-bit code 0..255 as SET_TRIGGER; detail 12), 14 u16 level2 (0; detail 14), 16 u32 holdoff
    (the trigger hold-off in 5 ns units, 0 = off: the raw 0x1B as GET_TRIGGER byte 12), 20 u32 0."""

    FIELDS = [("type", "B"), ("polarity", "B"), ("flags", "B"), ("source", "B"), ("n", "I"), ("h", "I"),
              ("level", "H"), ("level2", "H"), ("holdoff", "I"), ("reserved", "I")]
    type: int = TRIG_EDGE
    polarity: int = 0
    flags: int = 0
    source: int = 0
    n: int = 0
    h: int = 0
    level: int = 128
    level2: int = 0
    holdoff: int = 0
    reserved: int = 0

    @property
    def type_name(self) -> str:
        return TRIG_TYPE_NAMES[self.type] if self.type < len(TRIG_TYPE_NAMES) else str(self.type)

    @property
    def polarity_name(self) -> str:
        names = TRIG_POLARITY_NAMES.get(self.type, ("0", "1"))
        return names[self.polarity] if self.polarity < 2 else str(self.polarity)

    @property
    def longer(self) -> bool:
        return bool(self.flags & TRIG2_F_LONGER)

    @property
    def holdoff_s(self) -> float:
        return self.holdoff * TRIG2_HOLDOFF_UNIT_NS * 1e-9

    def describe(self) -> dict[str, Any]:
        d: dict[str, Any] = {"type": self.type_name, "polarity": self.polarity_name}
        if self.type == TRIG_PULSE_WIDTH:
            d["width_samples"] = self.n
            d["condition"] = "longer" if self.longer else "shorter"
            if self.flags & TRIG2_F_RANGE:
                d["width2_samples"] = self.h
                d["condition"] = "outside" if self.longer else "inside"
        elif self.type == TRIG_NTH_EDGE:
            d["n"] = self.n
            d["idle_samples"] = self.h
        elif self.type == TRIG_TIMEOUT:
            d["held_samples"] = self.n
        if self.type in (TRIG_RUNT, TRIG_WINDOW):
            d["level2_code"] = self.level2
        if self.flags & TRIG2_F_BOTH_CORES:
            d["both_cores"] = True
        d["holdoff_units"] = self.holdoff
        d["holdoff_s"] = self.holdoff_s
        return d


@dataclass
class Trigger(Payload):
    """GET_TRIGGER / SET_TRIGGER response: 28 bytes; 52 since protocol 1.26 (``trigger2`` = the SET_TRIGGER2 block at
    28..51, None in a 28-byte answer)."""

    FIELDS = [("source", "B"), ("edge", "B"), ("level", "B"), ("mode", "B"), ("pretrigger_samples", "I"),
              ("total_samples", "I"), ("holdoff", "I"), ("auto_timeout", "I"), ("pretrigger_words", "H"),
              ("total_words", "H"), ("reserved", "I")]
    source: int = 0
    edge: int = 0
    level: int = 128
    mode: int = 0
    pretrigger_samples: int = 0
    total_samples: int = 0
    #: raw 0x1B value: a trigger hold-off in 5 ns units since 0x153C (caps HOLDOFF), stored and unused before
    holdoff: int = 0
    auto_timeout: int = 0
    pretrigger_words: int = 0
    total_words: int = 0
    reserved: int = 0
    trigger2: "Trigger2 | None" = None

    def pack(self) -> bytes:
        out = super().pack()
        if self.trigger2 is not None:
            out += self.trigger2.pack()
        return out

    @classmethod
    def unpack(cls, data: bytes) -> "Trigger":
        t = super().unpack(data[:28])
        if len(data) >= 28 + Trigger2.size():
            t.trigger2 = Trigger2.unpack(data[28:28 + Trigger2.size()])
        return t

    @property
    def type(self) -> int:
        """The trigger type (TRIG_EDGE before 1.26)."""
        return self.trigger2.type if self.trigger2 is not None else TRIG_EDGE


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
    #: 1.17: segment-relative, address = segment << 16 | word offset from the segment's record start
    ADDR_SEGMENT: ClassVar[int] = 3
    #: layouts: 0 per core, 1 both cores of a channel interleaved, 2 (1.9) u16 per word (low byte core A), 3 (1.26)
    #: peak records: per selected channel ``count`` u8 minima (the core A slot) then ``count`` u8 maxima (core B slot)
    LAYOUT_CORES: ClassVar[int] = 0
    LAYOUT_INTERLEAVED: ClassVar[int] = 1
    LAYOUT_WIDE: ClassVar[int] = 2
    LAYOUT_PEAK: ClassVar[int] = 3


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
DECIM_FILTERS = ("off", "boxcar", "cic2", "cic3", "peak")
#: SET_DECIM filter 4 (protocol 1.26, FPGA 0x153C, caps PEAK_DETECT): peak detect, each stored word = { max, min } of
#: the window's raw samples (the firmware writes 0x53 K = 1 and 0x56 bit 1; the FIR is off, the shift unused)
DECIM_PEAK = 4
#: filter choices with the 1.11 FIR (GET_DECIM byte 1 bit 3 / SET_DECIM flags bit 3), as the UI's Filter box steps,
#: plus 1.26 "peak" (the ACQ menu's Sample row)
DECIM_FILTER_CHOICES = ("off", "boxcar", "boxcar+fir", "cic2", "cic2+fir", "cic3", "cic3+fir", "peak")
#: the shift byte (0x29 byte 1; GET_DECIM 3 / 25 / 27, GET_CAPTURE_INFO 69, READ_TRACE 31) since 1.26: bit 7 peak
#: active (0x56 bit 1 and K >= 1), bit 6 peak as written, bits 5..0 S
DECIM_PEAK_ACT = 0x80
DECIM_PEAK_EN = 0x40
DECIM_SHIFT_MASK = 0x3F


def parse_decim_filter(name: str) -> tuple[int, bool]:
    """'cic2+fir' -> (2, True); 'off' -> (0, False); 'peak' -> (4, False)."""
    n = name.strip().lower()
    if n not in DECIM_FILTER_CHOICES:
        raise ValueError(f"unknown filter {name!r}; use one of {', '.join(DECIM_FILTER_CHOICES)}")
    base, _, fir = n.partition("+")
    return DECIM_FILTERS.index(base), bool(fir)

#: 0x29 byte 0 bits besides K (bits 1:0). 0x53 carries K alone: bits 4..2 (the core-order swap, the falling-edge
#: capture) are gone in FPGA 0x153C and read 0 (fw-cruft; SET_DECIM flags bits 0..2 are accepted and dropped)
DECIM_WIDE = 0x80  # 0x29 bit 7: 16-bit layout active (K >= 1)
DECIM_FIR_EN = 0x40  # 0x29 bit 6 (1.11): 0x56 FIR enable as written
DECIM_FIR_ACT = 0x20  # 0x29 bit 5 (1.11): FIR active now (enable, K >= 1, N >= 16, N % 4 == 0)
#: the firmware's Filter setting bit for the FIR (SET_DECIM flags bit 3, shifted like the other flags)
DECIM_SET_FIR = 0x20
DECIM_FIR_MIN_N = 16
#: decim_config capability levels: bitstream without the filter, 0x1534 (boxcar / CIC), 0x1535+ (plus the FIR)
DECIM_HAS_NONE, DECIM_HAS_CIC, DECIM_HAS_FIR = 0, 1, 2
#: or-ed into ``has`` (1.24, FPGA 0x153A+ capture stages): the ADC data reach the memory 4 words (divider 0) / 3 words
#: later against EXT (legacy shift 12 / 14 samples, K >= 1 lag + 40 ns), and 0x53 bits 3 / 4 are written 0
DECIM_HAS_CAPSTAGES = 0x10
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


def ext_shift_legacy(divider: int, cap: bool = False) -> int:
    """1.22 (fw 0.2.72): EXT trigger in the legacy 8-bit layout, samples (two per word) by which the stored data lag
    the edge: the AD9288's 4-encode pipeline is 4 words at every legacy rate, 2 at divider 0 where the EXT
    synchroniser takes most of a 10 ns word (fpga/al3/README.md section 7 finding 4, tb_trigpos model numbers).
    1.24 (fw 0.2.79, ``cap`` = FPGA 0x153A+ capture stages): 12 at divider 0, 14 above (the IOB capture and the stage
    pick take 4 / 3 words more)."""
    if cap:
        return 14 if divider else 12
    return 8 if divider else 4


@dataclass
class DecimConfig:
    """Effective decimation for a setting (pc_decim_cfg): order K (0 = legacy 8-bit), shift S, why (DECIM_WHY
    index), factor N (0 legacy), gain (N^K; (N/2)^K with the FIR active; 1 legacy), FIR enable as written (1.11) and
    whether the FIR is active."""

    k: int = 0
    shift: int = 0
    why: int = 0
    n: int = 0
    gain: int = 1
    fir_en: bool = False
    fir: bool = False
    #: 1.24: FPGA 0x153A+ capture stages (DECIM_HAS_CAPSTAGES): the EXT shifts are longer
    cap: bool = False
    #: 1.26: peak detect active (k = 1, each word = { max, min } raw codes of the N-sample window, gain 1, shift 0)
    peak: bool = False
    #: 1.26: 0x56 bit 1 as written (the firmware writes it only for a peak record: never at divider 0 / long records)
    peak_en: bool = False

    @property
    def mode(self) -> int:
        """The 0x53 byte: K alone (bits 4..2 written 0)."""
        return self.k & 3

    @property
    def status(self) -> int:
        """The 0x29 byte 0 the FPGA reports."""
        return self.mode | (DECIM_WIDE if self.k else 0) | (DECIM_FIR_EN if self.fir_en else 0) | \
            (DECIM_FIR_ACT if self.fir else 0)

    @property
    def shift_byte(self) -> int:
        """The 0x29 byte 1 (1.26 layout): S in bits 5..0, bit 7 peak active, bit 6 peak as written."""
        return (self.shift & DECIM_SHIFT_MASK) | (DECIM_PEAK_ACT if self.peak else 0) | \
            (DECIM_PEAK_EN if self.peak_en else 0)

    @classmethod
    def from_status(cls, status: int, shift: int, n: int, gain: int | None = None, why: int = 0) -> "DecimConfig":
        """From a 0x29 byte 0 and the shift byte (GET_DECIM, GET_CAPTURE_INFO, READ_TRACE); the gain is (N/2)^K with
        the FIR active, 1 with peak detect (shift byte bit 7, 1.26)."""
        k = status & 3 if status & DECIM_WIDE else 0
        peak = bool(shift & DECIM_PEAK_ACT) and k > 0
        peak_en = bool(shift & DECIM_PEAK_EN)
        s = shift & DECIM_SHIFT_MASK
        fir = bool(status & DECIM_FIR_ACT) and k > 0 and not peak
        if peak:
            gain, s = 1, 0
        if gain is None:
            gain = decim_gain(n // 2 if fir else n, k) if k else 1
        return cls(k, s, why, n if k else 0, gain if k else 1, bool(status & DECIM_FIR_EN),
                   fir, peak=peak, peak_en=peak_en)

    @property
    def scale_q24(self) -> int:
        return decim_scale_q24(self.gain, self.shift) if self.k and not self.peak else 0

    @property
    def code_per_count(self) -> float:
        """ADC codes (mean of the filter window) per stored count: 2^S / gain (1 for peak records: raw codes)."""
        return (1 << self.shift) / self.gain if self.k and not self.peak else 1.0

    def rate(self, divider: int) -> int:
        if self.k and self.n:
            return BASE_CLOCK_HZ // self.n
        return BASE_CLOCK_HZ // (divider + 1)

    @property
    def ext_shift(self) -> int:
        """EXT trigger: output samples by which the stored data lag the edge, rounded: 120 ns + K (N - 1) 2.5 ns;
        with the FIR 250 ns + 17.5 N ns + 1.25 K (N - 2) ns; 40 ns more on FPGA 0x153A+ (``cap``, 1.24)."""
        if not self.k or not self.n:
            return 0
        if self.fir:
            lag, period = 1000 + 70 * self.n + 5 * self.k * (self.n - 2) + (160 if self.cap else 0), 20 * self.n
        else:
            lag, period = 240 + self.k * (self.n - 1) * 5 + (80 if self.cap else 0), self.n * 10
        return (lag + period // 2) // period

    def ext_shift_at(self, divider: int) -> int:
        """1.22 (fw 0.2.72): the EXT shift of a capture at this divider in samples of its layout: ext_shift with the
        filter, else the legacy one (ext_shift_legacy). Pretrigger words written: pretrigger - shift (16-bit) or
        (pretrigger - shift) / 2 (legacy); GET_CAPTURE_INFO 70 and GET_DECIM 31 report it, at most the pretrigger."""
        return self.ext_shift if self.k else ext_shift_legacy(divider, self.cap)

    @property
    def settle_us(self) -> int:
        """Filter settling after a change: (K + 2) output periods, us, rounded up; with the FIR (K + 10) N 5 ns + 1 us;
        peak detect 11 N 5 ns + 1 us (pc_decim_settle_us)."""
        if not self.k:
            return 0
        if self.peak:  # 1.26 (fw-c6): after the 0x56 write as after a filter write, K counted as 1
            return min((11 * self.n + 199) // 200 + 1, 0xFFFFFFFF)
        if self.fir:
            return min(((self.k + 10) * self.n + 199) // 200 + 1, 0xFFFFFFFF)
        return min(((self.k + 2) * self.n + 199) // 200, 0xFFFFFFFF)

    @property
    def filter_name(self) -> str:
        """Effective filter: 'cic2+fir' only while the FIR is active; 'peak' (1.26) while peak detect is active."""
        if not self.k:
            return "off"
        if self.peak:
            return "peak"
        return DECIM_FILTERS[self.k] + ("+fir" if self.fir else "")


def decim_fir_eligible(n: int) -> bool:
    return n >= DECIM_FIR_MIN_N and n % 4 == 0


def decim_config(k_req: int, flags: int, divider: int, total_samples: int, has: int = DECIM_HAS_CIC) -> DecimConfig:
    """pc_decim_config: the configuration an arm writes for the Filter setting ``k_req`` (0-3; 4 = peak detect since
    1.26), ``flags`` (DECIM_SET_FIR; other bits are ignored), the 0x0D divider, the capture length and the bitstream's
    level (DECIM_HAS_*, | DECIM_HAS_CAPSTAGES on 0x153A+). With the FIR active the CIC decimates by M = N / 2: gain
    M^K, S one higher, exactness on M. Peak detect runs the 16-bit path as K = 1 with the same legacy cases (divider 0,
    records over 8190 samples) and no FIR, shift 0, gain 1; the FIR request is dropped, and 0x56 bit 1 (``peak_en``,
    shift byte bit 6) is written only when the record is a peak record (fw-c6: not at divider 0 / long records)."""
    peak = k_req == DECIM_PEAK
    k, n = (1 if peak else k_req & 3), decim_n(divider)
    cap = bool(has & DECIM_HAS_CAPSTAGES)
    has &= ~DECIM_HAS_CAPSTAGES
    if has == DECIM_HAS_NONE:
        return DecimConfig(why=6, cap=cap)
    c = DecimConfig(fir_en=has >= DECIM_HAS_FIR and bool(flags & DECIM_SET_FIR) and not peak, cap=cap)
    if k == 0:
        c.why = 1
        return c
    if divider == 0:
        c.why = 2
        return c
    if total_samples > DECIM_MAX_TOTAL:
        c.why = 3
        return c
    if peak:
        c.k, c.n, c.gain, c.shift, c.peak, c.peak_en = 1, n, 1, 0, True, True
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
    """SET_DECIM: u8 filter (0 off, 1 boxcar, 2 CIC2, 3 CIC3, 4 peak detect since 1.26), u8 flags (bit 3 FIR; bits
    0..2, once the core-order swap and the falling-edge capture, are accepted and dropped since 1.27), u16 0."""

    FIELDS = [("filter", "B"), ("flags", "B"), ("_r", "2x")]
    filter: int = 0
    flags: int = 0

    #: 1.11 (caps FIR): the CIC compensation FIR
    FIR: ClassVar[int] = 0x08


@dataclass
class DecimState(Payload):
    """GET_DECIM / SET_DECIM response, 32 bytes; 36 from protocol 1.17 (features, UI Average as GET_STATUS 44..47)."""

    FIELDS = [("filter", "B"), ("flags", "B"), ("next_mode", "B"), ("next_shift", "B"), ("next_n", "I"),
              ("next_gain", "Q"), ("divider", "I"), ("rate_hz", "I"), ("last_mode", "B"), ("last_shift", "B"),
              ("fpga_mode", "B"), ("fpga_shift", "B"), ("max_total", "H"), ("why", "B"), ("ext_shift", "B"),
              ("features", "H"), ("ui_average", "B"), ("ui_average_log2n", "B")]
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
    #: 1.17: as GET_STATUS 44..47
    features: int = 0
    ui_average: int = 0
    ui_average_log2n: int = 0

    @property
    def next(self) -> DecimConfig:
        return DecimConfig.from_status(self.next_mode, self.next_shift, self.next_n, self.next_gain, self.why)

    @property
    def filter_setting(self) -> str:
        """The setting as the UI's Filter box names it ('cic2+fir'; 'peak' for the 1.26 Sample row)."""
        if self.filter == DECIM_PEAK:
            return "peak"
        return DECIM_FILTERS[self.filter & 3] + ("+fir" if self.flags & SetDecimReq.FIR and self.filter else "")


# Hardware counter (protocol 1.26, FPGA 0x153C read 0x35, docs/plans/fpga-0x153c.md section 5)

#: GET_COUNTER gate (0x61 bits 1..0): seconds, 0 = off
COUNTER_GATES = (0.0, 10e-3, 100e-3, 1.0)
#: GET_COUNTER source (0x61 bits 3..2): 0 follows the trigger source (0x15)
COUNTER_SOURCES = ("trigger", "CH1", "CH2", "EXT")
COUNTER_CLOCK_HZ = BASE_CLOCK_HZ  # E, T and W count 5 ns clocks
COUNTER_F_NEW = 0x01  # a gate ended since the last read
COUNTER_F_VALID = 0x02  # E >= 1: at least two edges in the gate
COUNTER_F_EXT = 0x04  # the effective source is EXT
COUNTER_F_CH2 = 0x08  # the effective source is CH2 (neither: CH1)
#: the firmware's boot thresholds (0x62) for the first gate; the auto rule replaces them after it
COUNTER_DEFAULT_LEVEL, COUNTER_DEFAULT_HYST = 128, 6
#: GET_COUNTER level / hysteresis 0 = auto (fw-ctrauto, the boot configuration): after each gate the firmware centres
#: the band on the gate's raw min / max midpoint, hysteresis max(6, round(span / 6)) at most span / 2; a non-zero
#: value pins that one
COUNTER_AUTO = 0
COUNTER_HYST_MIN = 6
#: auto: a gate's max - min below this is no signal (the thresholds stay, the result is not VALID)
COUNTER_AUTO_MIN_SPAN = 12


def counter_auto_target(mn: int, mx: int, level_cfg: int = COUNTER_AUTO, hyst_cfg: int = COUNTER_AUTO
                        ) -> tuple[int, int] | None:
    """The (level, hysteresis) the firmware's auto rule (fpga_counter.c cnt_auto_target) asks for after a gate with
    raw ``mn`` .. ``mx``; a pinned (non-zero) level or hysteresis is kept. None: no signal (span below
    COUNTER_AUTO_MIN_SPAN). The level is the upper threshold (high at >= level), the low one level - hysteresis."""
    span = mx - mn
    if span < COUNTER_AUTO_MIN_SPAN:
        return None
    h = hyst_cfg or min(max(COUNTER_HYST_MIN, (span + 3) // 6), span // 2)
    if level_cfg:
        return level_cfg, min(h, level_cfg)
    return min(255, (mn + mx + h + 1) // 2), h


def counter_auto_fits(level: int, hyst: int, target: tuple[int, int]) -> bool:
    """A gate counted at ``level`` / ``hyst`` fits ``target`` (counter_auto_target): both within max(2, h / 4) codes,
    the firmware's deadband against min / max moving by a code or two between gates."""
    band = max(2, target[1] // 4)
    return abs(level - target[0]) <= band and abs(hyst - target[1]) <= band


@dataclass
class CounterReq(Payload):
    """GET_COUNTER request (8 bytes): u8 what (0 read the snapshot, 1 write the configuration then read), u8 gate
    (COUNTER_GATES index), u8 source (COUNTER_SOURCES index), u8 level (raw code, Schmitt high at >= level; 0 =
    auto, the boot state), u8 hysteresis (codes below the level for low; 0 = auto), 3 x 0. With what = 0 the other
    fields are ignored. E_BAD_ARG detail 0 what, 1 gate, 2 source, 4 hysteresis > a non-zero level."""

    FIELDS = [("what", "B"), ("gate", "B"), ("source", "B"), ("level", "B"), ("hysteresis", "B"), ("_r", "3x")]
    what: int = 0
    gate: int = 0
    source: int = 0
    level: int = 0
    hysteresis: int = 0

    READ: ClassVar[int] = 0
    CONFIG: ClassVar[int] = 1


@dataclass
class CounterResp(Payload):
    """GET_COUNTER response (24 bytes): 0 u8 flags (COUNTER_F_*), 1 u8 seq (gate sequence number), 2 u8 gate, 3 u8
    source as configured (0 = the trigger source), 4 u32 E (periods between the first and the last rising edge in
    the gate; 0 = fewer than two edges), 8 u32 T (5 ns clocks between them), 12 u32 W (high clocks in them), 16 u8
    min, 17 u8 max (the source's raw codes over the gate; 255 / 0 when no sample), 18 u8 level, 19 u8 hysteresis (the
    thresholds in effect: the auto ones unless pinned), 20 u32 freq_mHz (the firmware's E / (T x 5 ns) in millihertz,
    saturating; 0 when invalid). VALID also needs the gate's thresholds to fit its min / max under auto (a gate
    counted while the thresholds were being retuned, or with no signal, is not VALID: E / T / W still given).
    Derived: frequency = E / (T x 5 ns), period = T x 5 ns / E, width = W x 5 ns / E, duty = W / T; resolution one
    clock over the gate (5e-7 relative at 10 ms, 5e-9 at 1 s), the 50 MHz crystal's accuracy on top."""

    FIELDS = [("flags", "B"), ("seq", "B"), ("gate", "B"), ("source", "B"), ("e", "I"), ("t", "I"), ("w", "I"),
              ("min", "B"), ("max", "B"), ("level", "B"), ("hysteresis", "B"), ("freq_mhz", "I")]
    flags: int = 0
    seq: int = 0
    gate: int = 2
    source: int = 0
    e: int = 0
    t: int = 0
    w: int = 0
    min: int = 255
    max: int = 0
    level: int = COUNTER_DEFAULT_LEVEL
    hysteresis: int = COUNTER_DEFAULT_HYST
    freq_mhz: int = 0

    @property
    def new(self) -> bool:
        return bool(self.flags & COUNTER_F_NEW)

    @property
    def valid(self) -> bool:
        return bool(self.flags & COUNTER_F_VALID) and self.e >= 1 and self.t > 0

    @property
    def gate_s(self) -> float:
        return COUNTER_GATES[self.gate] if self.gate < len(COUNTER_GATES) else 0.0

    @property
    def effective_source(self) -> str:
        """The channel the counter looked at ('CH1', 'CH2' or 'EXT' from the flags)."""
        if self.flags & COUNTER_F_EXT:
            return "EXT"
        return "CH2" if self.flags & COUNTER_F_CH2 else "CH1"

    @property
    def frequency_hz(self) -> float | None:
        return self.e * COUNTER_CLOCK_HZ / self.t if self.valid else None

    @property
    def period_s(self) -> float | None:
        return self.t / COUNTER_CLOCK_HZ / self.e if self.valid else None

    @property
    def width_s(self) -> float | None:
        return self.w / COUNTER_CLOCK_HZ / self.e if self.valid else None

    @property
    def duty(self) -> float | None:
        return self.w / self.t if self.valid else None

    @property
    def resolution(self) -> float | None:
        """Relative resolution of the frequency: one clock in T."""
        return 1.0 / self.t if self.valid else None


def counter_freq_mhz(e: int, t: int) -> int:
    """The firmware's byte 20: round(E x 200e6 x 1000 / T), saturating at 2^32 - 1; 0 without a valid count."""
    if e < 1 or t <= 0:
        return 0
    return min((e * COUNTER_CLOCK_HZ * 1000 + t // 2) // t, 0xFFFFFFFF)


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


# --- on-device Bode plot (protocol 1.20, section 14 row 60) ---------------------------------------------------

#: BODE_CTRL actions
BODE_STATUS, BODE_RUN, BODE_REPEAT, BODE_STOP, BODE_ABORT, BODE_THRU_MEASURE, BODE_THRU_ON, BODE_THRU_OFF, \
    BODE_THRU_CLEAR, BODE_OPEN, BODE_CLOSE = range(11)
#: BODE_CTRL flags: bit 0 the request's settings apply (to this and later host sweeps), bit 1 no background captures
BODE_F_SETTINGS = 0x01
BODE_F_NO_BACKGROUND = 0x02
#: settle presets (byte 2) in the host tool's names, and their periods (bode.SETTLE_PRESETS)
BODE_SETTLES = ("fast", "normal", "resonant", "high_q")
#: CH2 range mode (byte 3): 0 auto, 1 hold (CH2's range when the sweep starts = the host tool's 'fixed')
BODE_CH2_MODES = ("auto", "fixed")
#: READ_BODE header state / reason
BODE_STATES = ("idle", "running", "done", "stopped", "error")
BODE_REASONS = {0: None, 1: "the FPGA did not become ready", 2: "no capture within twice the record time + 1 s",
                3: "CH2 saturated by a DC level with the CH2 range held", 4: "no frequency in the range",
                5: "the PC armed its own capture during the sweep", 6: "stopped",
                7: "no decimation filter or no DDS generator on this bitstream",
                8: "ADC readout fault: the capture self-check flagged a core (0x153A+), or the pre-scan clipped at "
                   "5 V/div (older bitstreams)"}
#: READ_BODE header byte 7 bits 0-6
BODE_THRU_STATES = ("none", "being measured", "valid", "stale")
#: READ_BODE row flag bits in the host tool's flag names (bode.py); bit 14 "retried" is the host's retry count, bit 15
#: marks a row not measured in the latest sweep (the rest of the row 0)
BODE_ROW_FLAGS = ("ch1_clipped", "clipped", "ch2_overload", "ch1_low", "ch2_small", "low_snr", "near_floor",
                  "unsettled", "incoherent", "spliced", "fir_off", "no_cal", "cal_range", "cal_flagged")
BODE_ROW_RETRIED = 0x4000
BODE_ROW_NOT_MEASURED = 0x8000
BODE_HDR_BYTES = 32
BODE_ROW_BYTES = 20
#: rows that fit one READ_BODE answer (count 0 asks for them)
BODE_MAX_ROWS = (MAX_RESPONSE_PAYLOAD - BODE_HDR_BYTES) // BODE_ROW_BYTES


@dataclass
class BodeCtrlReq(Payload):
    """BODE_CTRL request, 16 bytes: u8 action, u8 flags, u8 settle (0..3), u8 CH2 range mode (0 auto, 1 hold), u32
    start cHz, u32 stop cHz (1 Hz .. 10 MHz, either order), u16 points (1..200), u16 0."""

    FIELDS = [("action", "B"), ("flags", "B"), ("settle", "B"), ("ch2_mode", "B"), ("start_chz", "I"),
              ("stop_chz", "I"), ("points", "H"), ("_r0", "2x")]
    action: int = BODE_STATUS
    flags: int = 0
    settle: int = 1
    ch2_mode: int = 0
    start_chz: int = 0
    stop_chz: int = 0
    points: int = 0

    @classmethod
    def make(cls, action: int, start_hz: float | None = None, stop_hz: float | None = None, points: int = 50,
             settle: int = 1, ch2_mode: int = 0, background: bool = True) -> "BodeCtrlReq":
        """With ``start_hz`` / ``stop_hz`` the settings apply (flags bit 0); without, the screen's settings are used."""
        if start_hz is None or stop_hz is None:
            return cls(action, 0 if background else BODE_F_NO_BACKGROUND)
        return cls(action, BODE_F_SETTINGS | (0 if background else BODE_F_NO_BACKGROUND), settle, ch2_mode,
                   int(round(start_hz * 100)), int(round(stop_hz * 100)), points)


@dataclass
class BodeHeader(Payload):
    """READ_BODE response header (also the first 32 bytes of the BODE_CTRL answer)."""

    FIELDS = [("sweep_id", "I"), ("state", "B"), ("reason", "B"), ("view", "B"), ("thru", "B"), ("planned", "H"),
              ("measured", "H"), ("current", "h"), ("settle", "B"), ("ch2_mode", "B"), ("start_chz", "I"),
              ("stop_chz", "I"), ("points", "H"), ("switches", "H"), ("elapsed_ms", "I")]
    #: increments with every start; 0 = none since boot
    sweep_id: int = 0
    state: int = 0
    reason: int = 0
    #: bit 0 the view shows, bit 1 RUN repeats sweeps, bit 2 this sweep is a thru measurement, bits 3-4 CH1 / CH2's
    #: offset reached the PWM limit
    view: int = 0
    #: bits 0-6 BODE_THRU_STATES, bit 7 applied
    thru: int = 0
    planned: int = 0
    measured: int = 0
    #: the point being measured (-1 none)
    current: int = -1
    settle: int = 1
    ch2_mode: int = 0
    start_chz: int = 0
    stop_chz: int = 0
    points: int = 0
    #: CH2 range switches in this sweep; fw 0.2.93: the unplanned ones (ranging, a clip, a drop below the floor), the
    #: planned ones are ``BodeCtrlResp.planned_switches``
    switches: int = 0
    #: ms since the sweep started (its duration once it ended)
    elapsed_ms: int = 0

    V_SHOWN: ClassVar[int] = 0x01
    V_REPEAT: ClassVar[int] = 0x02
    V_THRU_SWEEP: ClassVar[int] = 0x04
    V_CH1_OFFSET_LIMIT: ClassVar[int] = 0x08
    V_CH2_OFFSET_LIMIT: ClassVar[int] = 0x10

    @property
    def state_name(self) -> str:
        return BODE_STATES[self.state] if self.state < len(BODE_STATES) else f"state {self.state}"

    @property
    def reason_text(self) -> str | None:
        return BODE_REASONS.get(self.reason, f"reason {self.reason}")

    @property
    def thru_state(self) -> str:
        t = self.thru & 0x7F
        return BODE_THRU_STATES[t] if t < len(BODE_THRU_STATES) else f"thru {t}"

    @property
    def thru_applied(self) -> bool:
        return bool(self.thru & 0x80)

    @property
    def running(self) -> bool:
        return self.state == 1


@dataclass
class BodeCtrlResp(Payload):
    """BODE_CTRL response, 40 bytes: the READ_BODE header, u32 the estimated duration of a sweep with the settings in
    effect (ms), u32 0. fw 0.2.91: 48 bytes, then the latest sweep's capture statistics (u16 each): generator-on
    captures, those that ended on the auto time-out (no CH1 trigger), records taken again for CH1 leak (a joined
    record) and for another reason (suspect, time-out). fw 0.2.93: 50 bytes, then the latest sweep's planned CH2
    range switches (range changes between neighbouring points that the range plan prescribes; the header's
    ``switches`` are the unplanned ones). fw 0.2.94: 58 bytes, then the latest on-screen AUTO SET's CH2 range verdict
    (0 Auto, 1 Constant (the host's 'fixed' / hold), 0xFF none since boot), its reason (0 one range holds the span, 1
    a spread of 20 dB or more, 2 fewer than two points in the span, 3 a point without a tone, 4 no range holds the
    largest level), the points that counted and those left out (a bad record), i16 the spread in centi-dB and i16 the
    largest CH2 level in centi-dBV peak. Older firmware: all 0 (so ``auto_ch2_verdict`` 0 means nothing there)."""

    FIELDS = [("header", "32s"), ("estimate_ms", "I"), ("_r0", "4x"), ("captures", "H"), ("timeouts", "H"),
              ("leak_retakes", "H"), ("other_retakes", "H"), ("planned_switches", "H"), ("auto_ch2_verdict", "B"),
              ("auto_ch2_reason", "B"), ("auto_ch2_points", "B"), ("auto_ch2_left_out", "B"),
              ("auto_ch2_spread_cdb", "h"), ("auto_ch2_vmax_cdbv", "h")]
    header: bytes = bytes(32)
    estimate_ms: int = 0
    captures: int = 0
    timeouts: int = 0
    leak_retakes: int = 0
    other_retakes: int = 0
    planned_switches: int = 0
    auto_ch2_verdict: int = 0
    auto_ch2_reason: int = 0
    auto_ch2_points: int = 0
    auto_ch2_left_out: int = 0
    auto_ch2_spread_cdb: int = 0
    auto_ch2_vmax_cdbv: int = 0

    @property
    def hdr(self) -> BodeHeader:
        return BodeHeader.unpack(self.header)


@dataclass
class ReadBodeReq(Payload):
    """READ_BODE request, 8 bytes: u16 first row, u16 count (0 or more than fit = all that fit), u32 0."""

    FIELDS = [("first", "H"), ("count", "H"), ("_r0", "4x")]
    first: int = 0
    count: int = 0


@dataclass
class BodeRow(Payload):
    """One READ_BODE row, 20 bytes."""

    FIELDS = [("period", "I"), ("f_chz", "I"), ("mag_cdb", "h"), ("phase_cdeg", "h"), ("v1_cdbv", "h"),
              ("v2_cdbv", "h"), ("ch1_range", "B"), ("ch2_range", "B"), ("flags", "H")]
    #: stimulus period P in 5 ns clocks (protocol 1.27: f = 200 MHz / P exactly; up to 1.26 it was M in 50 MHz DDS
    #: clocks, P = 4 M)
    period: int = 0
    #: that f in cHz, rounded
    f_chz: int = 0
    #: |H| in centi-dB, arg H in centi-degrees (-180 .. +180]
    mag_cdb: int = 0
    phase_cdeg: int = 0
    #: the fundamentals at the probe tips in centi-dBV RMS (probe factor included; V2 with the splice)
    v1_cdbv: int = 0
    v2_cdbv: int = 0
    ch1_range: int = 0
    ch2_range: int = 0
    flags: int = 0

    @property
    def measured(self) -> bool:
        return not self.flags & BODE_ROW_NOT_MEASURED

    @property
    def f_hz(self) -> float:
        """The exact stimulus frequency from P (the f_chz field is rounded)."""
        return BASE_CLOCK_HZ / self.period if self.period else 0.0

    @property
    def flag_names(self) -> list[str]:
        return [n for i, n in enumerate(BODE_ROW_FLAGS) if self.flags >> i & 1]

    @property
    def retried(self) -> bool:
        return bool(self.flags & BODE_ROW_RETRIED)


@dataclass
class CalProbeReq(Payload):
    """CAL_PROBE request (protocol 1.21, row 61): u8 channel, u8 hardware range 0..5 (0 = 5 V/div, the wizard's
    "range 1"), u8 settle reads 0..15 (extra data-port reads before the sampled read), u8 prestrobe 0 / 1, u8 mode (0
    one range, 1 the whole wizard zero step in its screen context, both channels, every range: about 15 s)."""

    FIELDS = [("channel", "B"), ("range", "B"), ("settle", "B"), ("prestrobe", "B"), ("mode", "B")]
    channel: int = 0
    range: int = 0
    settle: int = 0
    prestrobe: int = 0
    mode: int = 0


#: CAL_PROBE rc (ACAL_* of adc_cal.h); -1 = mode 1 cancelled on the screen
ACAL_RESULTS = {0: "OK", 4: "RESPONSE", 5: "CLIP", 6: "NOISE", 7: "ZERO", 8: "SPREAD", 9: "SLOPE", 10: "DRIFT",
                11: "CLASS", 18: "TIMING", 19: "READOUT", -1: "CANCELLED"}
#: 11 (CLASS: full-rate and gated zeros apart) is retired since fw 0.2.105 (one calibration class, FPGA 0x153D: the
#: ADCs always run at full rate); the name stays for older answers
ACAL_RESULT_TEXT = {
    0: "passed", 4: "no response to the offset (the input is not at 0 V, or the front end does not move)",
    5: "the input is not at 0 V: a sample at code 0 or 255", 6: "too much noise", 7: "zero out of range",
    8: "the cores' zeros are too far apart", 9: "offset slope out of range", 10: "the zero drifted between captures",
    11: "retired (fw before 0.2.105: the gated and full-rate classes disagreed)",
    18: "ADC capture timing: the 200 MSa/s data stays garbled",
    19: "ADC readout fault: a core has no clean capture stage (the capture stage training failed)",
    -1: "cancelled on the screen"}
#: CAL_PROBE point order (row 61). Point 4 (the gated zero) is not measured since fw 0.2.105 (one calibration class);
#: points 0..3 are taken at 2 MSa/s (the centre search), 5..7 at 200 MSa/s (the calibration's values)
CAL_PROBE_POINTS = ("coarse high (PWM 500)", "coarse low (PWM 1200)", "slope low (centre - 150, 2 MSa/s)",
                    "slope high (centre + 150, 2 MSa/s)", "retired (gated zero, not measured since fw 0.2.105)",
                    "full-rate zero (pair)", "full-rate high (+150)", "full-rate low (-150)")
#: CAL_PROBE point / failure "class" byte: since fw 0.2.105 the rate the point was measured at (0 200 MSa/s, 1 2 MSa/s)
CAL_PROBE_RATES = ("200 MSa/s", "2 MSa/s")
CAL_PROBE_BYTES = 252
#: CAL_PROBE flags (byte 7)
CALP_GARBLED, CALP_NO_CENTRE, CALP_PRESTROBE, CALP_MODE1 = 0x01, 0x02, 0x04, 0x08
#: the wizard's noise limit (RMS about the mean of all conversions, 1/16 code; firmware adc_cal.h ACAL_MAX_NOISE16, the
#: Diagnostics limit too): 3 codes for every point since fw 0.2.105 (one calibration class)
CAL_NOISE_LIMIT16 = 48

_CALP_HDR = struct.Struct("<BBhBBBBiIHHHH")
_CALP_CORE = struct.Struct("<iHHBBH")


@dataclass
class CalProbeCore:
    """One core of a CAL_PROBE point (codes, not 1/16)."""

    mean: float
    rms: float
    rms_within: float
    lo: int
    hi: int
    clipped: int


@dataclass
class CalProbePoint:
    index: int
    pwm: int
    cls: int
    conversions: int
    cores: tuple[CalProbeCore, CalProbeCore]

    @property
    def name(self) -> str:
        return CAL_PROBE_POINTS[self.index] if self.index < len(CAL_PROBE_POINTS) else f"point {self.index}"

    @property
    def measured(self) -> bool:
        return self.conversions > 0


@dataclass
class CalProbeResp:
    """CAL_PROBE response, 252 bytes (protocol 1.21, row 61). ``split_d`` (bytes 248..249) is the zero split of
    fw 0.2.67-0.2.78 (the zero measured as two points either side of the centre PWM); always 0 since fw 0.2.79,
    where the wizard measures the zero at the centre again (row 64)."""

    channel: int
    range: int
    rc: int
    fail_point: int
    fail_class: int
    fail_core: int
    flags: int
    fail_value: int
    fail_within16: int
    coarse_centre: int
    zero_pwm: int
    enc_bad: int
    enc_recovered: int
    points: list[CalProbePoint]
    split_d: int = 0

    @property
    def settle_reads(self) -> int:
        return self.flags >> 4

    @property
    def result(self) -> str:
        return ACAL_RESULTS.get(self.rc, f"rc {self.rc}")

    @classmethod
    def unpack(cls, b: bytes) -> "CalProbeResp":
        if len(b) < 248:
            raise ValueError(f"CAL_PROBE answer of {len(b)} bytes (needs 248 or 252)")
        h = _CALP_HDR.unpack_from(b, 0)
        pts = []
        for i in range(8):
            o = 24 + 28 * i
            pwm, c, n = struct.unpack_from("<HBB", b, o)
            cores = []
            for k in range(2):
                m, r, w, lo, hi, cl = _CALP_CORE.unpack_from(b, o + 4 + 12 * k)
                cores.append(CalProbeCore(m / 16, r / 16, w / 16, lo, hi, cl))
            pts.append(CalProbePoint(i, pwm, c, n, (cores[0], cores[1])))
        split = struct.unpack_from("<H", b, 248)[0] if len(b) >= 250 else 0
        return cls(*h, pts, split)

    def pack(self) -> bytes:
        out = bytearray(_CALP_HDR.pack(self.channel, self.range, self.rc, self.fail_point, self.fail_class,
                                       self.fail_core, self.flags, self.fail_value, self.fail_within16,
                                       self.coarse_centre, self.zero_pwm, self.enc_bad, self.enc_recovered))
        for p in self.points:
            out += struct.pack("<HBB", p.pwm, p.cls, p.conversions)
            for c in p.cores:
                out += _CALP_CORE.pack(int(round(c.mean * 16)), min(0xFFFF, int(round(c.rms * 16))),
                                       min(0xFFFF, int(round(c.rms_within * 16))), c.lo, c.hi, min(0xFFFF, c.clipped))
        out += struct.pack("<HH", self.split_d, 0)
        return bytes(out)

    def describe(self) -> dict[str, Any]:
        """The answer in words: the verdict, the failing point and every measured point per core."""
        fail = None
        if self.rc not in (0, -1) and self.fail_point != 0xFF:
            fail = {"point": CAL_PROBE_POINTS[self.fail_point] if self.fail_point < 8 else self.fail_point,
                    "rate": CAL_PROBE_RATES[self.fail_class & 1], "core": "AB"[self.fail_core & 1],
                    "value": self.fail_value, "within_capture_rms_codes": round(self.fail_within16 / 16, 3)}
            if self.rc in (6, 10):
                fail["value_codes"] = round(self.fail_value / 16, 3)
        pts = []
        for p in self.points:
            if not p.measured:
                continue
            pts.append({"point": p.name, "pwm": p.pwm, "rate": CAL_PROBE_RATES[p.cls & 1],
                        "conversions": p.conversions,
                        **{f"core_{'AB'[k]}": {"mean": round(c.mean, 3), "rms": round(c.rms, 3),
                                                "rms_within": round(c.rms_within, 3), "lowest": c.lo,
                                                "highest": c.hi, "clipped": c.clipped,
                                                "noisy": round(c.rms * 16) > CAL_NOISE_LIMIT16}
                           for k, c in enumerate(p.cores)}})
        return {"channel": f"CH{self.channel + 1}", "range": self.range,
                "volts_per_div": RANGE_VOLTS_PER_DIV[self.range] if self.range < 6 else None,
                "result": self.result, "text": ACAL_RESULT_TEXT.get(self.rc, ""), "failure": fail,
                "coarse_centre_pwm": self.coarse_centre or None, "zero_pwm": self.zero_pwm or None,
                "zero_split_pwm": self.split_d or None, "settle_reads": self.settle_reads,
                "prestrobe": bool(self.flags & CALP_PRESTROBE), "whole_zero_step": bool(self.flags & CALP_MODE1),
                "full_rate_accepted_garbled": bool(self.flags & CALP_GARBLED),
                "coarse_centre_failed": bool(self.flags & CALP_NO_CENTRE),
                "encode_check": {"garbled": self.enc_bad, "recovered": self.enc_recovered}, "points": pts}


def bode_flags_from_names(names: Iterable[str], retried: bool = False) -> int:
    """The row flag word for host flag names (unknown names are ignored)."""
    f = 0
    for n in names:
        if n in BODE_ROW_FLAGS:
            f |= 1 << BODE_ROW_FLAGS.index(n)
    return f | (BODE_ROW_RETRIED if retried else 0)


def parse_read_bode(payload: bytes) -> tuple[BodeHeader, list[BodeRow]]:
    """READ_BODE: the 32-byte header, then 20-byte rows (as many as the answer holds)."""
    h = BodeHeader.unpack(payload[:BODE_HDR_BYTES])
    n = max(0, len(payload) - BODE_HDR_BYTES) // BODE_ROW_BYTES
    rows = [BodeRow.unpack(payload[BODE_HDR_BYTES + BODE_ROW_BYTES * i: BODE_HDR_BYTES + BODE_ROW_BYTES * (i + 1)])
            for i in range(n)]
    return h, rows


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


#: GEN byte 1 = FPGA 0x39 byte 0, the square's status (protocol 1.27)
GEN_ST_RUNNING = 0x80  # the square runs
GEN_ST_PENDING = 0x20  # a committed change (SET_GEN, a chirp step) not yet taken at a step boundary
GEN_ST_STEP = 0x01  # the entry running: 0 high, 1 low
#: GEN byte 22 = FPGA 0x39 byte 3
GEN_ST3_ACTIVE = 0x01  # mode 3 (continuous) in effect
#: SET_GEN's frequency cap (unchanged from the DDS); the square needs n_h, n_l >= 2 clocks of 5 ns
GEN_MAX_MHZ = 25_000_000_000
GEN_MIN_STEP_CLOCKS = 2
GEN_MAX_STEP_CLOCKS = (1 << 32) - 1
#: 2^16 x 2e11: P16 x f_mHz (2e11 = 200e6 clocks/s x 1000 mHz/Hz; P16 in 1/65536 clock)
_GEN_K = (1 << 16) * 200_000_000_000


@dataclass
class Gen(Payload):
    """GEN response (GET_GEN / SET_GEN): 24 bytes up to protocol 1.26 (the DDS words at 4 / 8), 40 since 1.27 (FPGA
    0x153D: the generator is a fractional two-step square on the 5 ns clock, contract 7.2 / 7.4): 0 u8 enable (0x50:
    the pin is driven), 1 u8 status (FPGA 0x39 byte 0: GEN_ST_RUNNING, GEN_ST_PENDING = a commit not yet taken,
    GEN_ST_STEP), 2..11 0 (4 / 8 were the retired 0x51 / 0x52 words), 12 u64 freq_mhz_actual, 20 u16 duty_bp_actual,
    22 u8 status3 (0x39 byte 3: GEN_ST3_ACTIVE), 23 u8 0, 24 u64 p16 (the period in 1/65536 of a 5 ns clock), 32 u64 h16 (the high part, same unit). GET_GEN
    reads 0x39 live (from the main loop): poll it until ``pending`` clears before the next glitch-free change (the
    host chirp's pace)."""

    FIELDS = [("enable", "B"), ("status", "B"), ("_r2", "10x"),
              ("freq_mhz_actual", "Q"), ("duty_bp_actual", "H"), ("status3", "B"), ("_r23", "x"),
              ("p16", "Q"), ("h16", "Q")]
    enable: int = 0
    status: int = 0
    freq_mhz_actual: int = 0
    duty_bp_actual: int = 0
    status3: int = 0
    p16: int = 0
    h16: int = 0

    @property
    def frequency_hz(self) -> float:
        """The exact average frequency 200 MHz x 65536 / P16 (the mHz field is rounded)."""
        if self.p16:
            return GEN_CLOCK_HZ * 65536 / self.p16
        return self.freq_mhz_actual / 1000.0

    @property
    def duty(self) -> float:
        if self.p16:
            return self.h16 / self.p16
        return self.duty_bp_actual / 10000.0

    @property
    def pending(self) -> bool:
        """A committed change (SET_GEN, a chirp step) not yet taken at a step boundary (0x39 byte 0 bit 5)."""
        return bool(self.status & GEN_ST_PENDING)

    @property
    def running(self) -> bool:
        return bool(self.status & GEN_ST_RUNNING)

    def steps(self) -> tuple[int, int, int, int]:
        """(n_h, f_h, n_l, f_l): the two entries the firmware writes for this square."""
        return gen_steps(self.p16, self.h16)


@dataclass
class SetGenReq(Payload):
    """SET_GEN request, 20 bytes: 0 u8 mask (M_ENABLE, M_FREQ, M_DUTY), 1 u8 enable, 2 u8 freq_mode (0 ``freq`` in
    mHz, 1 the raw period P16), 3 u8 duty_mode (0 ``duty`` in basis points, 1 the raw high part H16), 4 u64 freq,
    12 u64 duty: with duty_mode 0 the u32 duty in bp and the u32 reserved (0) of protocol 1.0-1.26; with duty_mode 1
    (1.27) bytes 12..19 are one u64 H16. E_BAD_ARG detail = the mask bit: 1 the frequency (0, above 25 MHz, or a
    step of the square under 2 or over 2^32 - 1 clocks), 2 the duty (over 10000 bp)."""

    FIELDS = [("mask", "B"), ("enable", "B"), ("freq_mode", "B"), ("duty_mode", "B"), ("freq", "Q"), ("duty", "Q")]
    mask: int = 0
    enable: int = 0
    freq_mode: int = 0
    duty_mode: int = 0
    freq: int = 0
    duty: int = 0

    M_ENABLE: ClassVar[int] = 1
    M_FREQ: ClassVar[int] = 2
    M_DUTY: ClassVar[int] = 4


def gen_p16(freq_mhz: int) -> int:
    """The firmware's period for f in mHz (contract 7.4): P16 = (2^16 x 2e11 + f / 2) / f, in 1/65536 clock."""
    if freq_mhz <= 0:
        raise ValueError("frequency must be > 0")
    return (_GEN_K + freq_mhz // 2) // freq_mhz


def gen_h16(p16: int, duty_bp: int) -> int:
    """H16 = (P16 x d + 5000) / 10000, d in basis points."""
    return (p16 * duty_bp + 5000) // 10000


def gen_freq_mhz(p16: int) -> int:
    """GEN freq_mhz_actual: (2^16 x 2e11 + P16 / 2) / P16."""
    return (_GEN_K + p16 // 2) // p16 if p16 else 0


def gen_duty_bp(h16: int, p16: int) -> int:
    """GEN duty_bp_actual: (H16 x 10000 + P16 / 2) / P16."""
    return (h16 * 10000 + p16 // 2) // p16 if p16 else 0


def gen_steps(p16: int, h16: int) -> tuple[int, int, int, int]:
    """(n_h, f_h, n_l, f_l): entry 0 (high) and entry 1 (low, end) of the square, integer clocks and 1/65536."""
    lo = p16 - h16
    return h16 >> 16, h16 & 0xFFFF, lo >> 16, lo & 0xFFFF


def gen_check(p16: int, h16: int) -> str | None:
    """Why the firmware refuses this square (E_BAD_ARG detail 1), or None."""
    if p16 <= 0 or p16 < gen_p16(GEN_MAX_MHZ):
        return "frequency above 25 MHz"
    n_h, _, n_l, _ = gen_steps(p16, h16)
    if min(n_h, n_l) < GEN_MIN_STEP_CLOCKS:
        return f"a step of {min(n_h, n_l)} clocks (the square needs at least {GEN_MIN_STEP_CLOCKS} x 5 ns high and low)"
    if max(n_h, n_l) > GEN_MAX_STEP_CLOCKS:
        return "a step longer than 2^32 - 1 clocks (21.47 s)"
    return None


def gen_period_p16(period_clocks: int) -> tuple[int, int]:
    """(P16, H16) of a strictly periodic square of an integer period (the Bode stimulus, contract 7.4): fraction 0,
    n_h = ceil(P / 2), n_l = floor(P / 2)."""
    return period_clocks << 16, ((period_clocks + 1) // 2) << 16


# ---------------------------------------------------------------------------------------------------------
# Streaming logger (protocol 1.27, FPGA 0x153D, contract section 6)

LOG_FMT_RAW8, LOG_FMT_HIGH8, LOG_FMT_WIDE16, LOG_FMT_PEAK = 0, 1, 2, 3
#: format names: raw 8-bit legacy samples (both cores, filter off), the filtered word's high byte, the filtered
#: 16-bit word, peak-detect min / max pairs
LOG_FORMAT_NAMES = ("raw8", "filtered8", "filtered16", "peak")
#: bytes per sample per channel
LOG_SAMPLE_BYTES = (1, 1, 2, 2)
#: the documented rate tiers (contract 6.2, owner 2026-10-20 22:38): 500 kS/s one 8-bit channel, 250 kS/s one 16-bit
#: or two 8-bit, 125 kS/s two 16-bit: sample rate x bytes per sample x channels <= 500 kB/s
LOG_MAX_BYTES_PER_S = 500_000
LOG_MAX_DIVIDER = (1 << 20) - 1
LOG_STATES = ("idle", "running", "ended", "stopped", "error")
LOG_IDLE, LOG_RUNNING, LOG_ENDED, LOG_STOPPED, LOG_ERROR = range(5)
#: LOG flags (byte 3)
LOG_F_LAP = 0x01  # an FPGA ring lap since the start (the MCU fell behind)
LOG_F_OVERFLOW = 0x02  # the DDR ring overflowed since the start (the host fell behind)
#: LOG_READ chunk flags (header byte 12)
LOG_CHUNK_RUNNING = 0x01
LOG_CHUNK_GAP = 0x02  # the word counter jumps before this chunk (data lost)
LOG_CHUNK_ENDED = 0x04  # the log ended: this chunk holds its last bytes (or none)
LOG_CHUNK_OVERFLOW = 0x08  # with GAP: the gap includes words dropped because the DDR ring was full
LOG_CHUNK_HEADER_BYTES = 16
LOG_READ_MAX = 65536
#: LOG_START E_BAD_ARG details
LOG_D_CHANNELS, LOG_D_RATE, LOG_D_FORMAT, LOG_D_DIVIDER = 0, 1, 2, 4
#: E_STATE detail while logging (every other acquisition command)
LOG_E_STATE_LOGGING = 9
#: what a scope answers while a log runs (fw-d4, docs/protocol.md row 67): the F_BUSY set, the read-only set and the
#: logger's LOG_READ / LOG_STOP; everything else (LOG_START, ARM, every SET_* including the generator: set them up
#: before the log) gets E_STATE detail LOG_E_STATE_LOGGING "logging: LOG_STOP first"
LOG_ALLOWED = frozenset({
    Cmd.PING, Cmd.GET_INFO, Cmd.GET_STATUS, Cmd.SET_REMOTE, Cmd.GET_BATTERY, Cmd.GET_LINK_STATS, Cmd.GET_BAD_FRAME,
    Cmd.GET_EVENT_LOG, Cmd.GET_HEALTH, Cmd.GET_CAPTURE_INFO, Cmd.SD_STAGE, Cmd.READ_SCREEN, Cmd.GET_CHANNEL,
    Cmd.GET_TIMEBASE, Cmd.GET_TRIGGER, Cmd.GET_DECIM, Cmd.GET_SEQ, Cmd.GET_GEN, Cmd.GET_CAL, Cmd.READ_BODE,
    Cmd.GET_COUNTER, Cmd.LOG_READ, Cmd.LOG_STOP})
#: LOG_START E_STATE details (row 67): 0 a host capture is armed, 10 not in the Scope view (fw-roll: a rolling UI no
#: longer refuses with detail 2, it stops as for a host capture)
LOG_S_ARMED, LOG_S_VIEW = 0, 10
#: the log ends in state error when the host does not read for this long (row 67)
LOG_HOST_TIMEOUT_S = 10.0


@dataclass
class LogStartReq(Payload):
    """LOG_START 0x0214 request (16 bytes): u8 channels (bit 0 CH1, bit 1 CH2), u8 format (LOG_FMT_*), u16 0, u32
    divider (the 0x0D value; the filter K / FIR / shift from the current SET_DECIM), u32 duration_ms (0 = until
    LOG_STOP; the log ends after exactly ceil(duration_ms x 200,000 / word clocks) words), u32 0. E_BAD_ARG detail 0
    channels, 1 rate (over LOG_MAX_BYTES_PER_S), 2 format (0 needs the filter off, 1 / 2 filter 1..3, 3 filter 4 peak;
    1 / 2 / 3 at divider 0), 4 divider (over 2^20 - 1). E_STATE detail 0 a host capture armed, 10 not in the Scope
    view (GET_STATUS 43 not 0 or 4), 9 while logging."""

    FIELDS = [("channels", "B"), ("format", "B"), ("_r0", "2x"), ("divider", "I"), ("duration_ms", "I"),
              ("_r1", "4x")]
    channels: int = 1
    format: int = LOG_FMT_HIGH8
    divider: int = 399
    duration_ms: int = 0


@dataclass
class LogResp(Payload):
    """LOG response of LOG_START / LOG_STOP (48 bytes): 0 u8 state (LOG_STATES), 1 u8 channels, 2 u8 format, 3 u8
    flags (LOG_F_*), 4 u32 divider, 8 u32 duration_ms, 12 u32 sample_clocks (5 ns clocks per sample per channel),
    16 u64 bytes produced since the start, 24 u64 bytes sent to the host, 32 u32 gaps (FPGA laps and DDR overflows),
    36 u32 the DDR ring's size in bytes, 40 u32 ms since the start (frozen when the log ends), 44 u32 0."""

    FIELDS = [("state", "B"), ("channels", "B"), ("format", "B"), ("flags", "B"), ("divider", "I"),
              ("duration_ms", "I"), ("sample_clocks", "I"), ("produced", "Q"), ("sent", "Q"), ("gaps", "I"),
              ("ring_bytes", "I"), ("elapsed_ms", "I"), ("_r", "4x")]
    state: int = LOG_IDLE
    channels: int = 0
    format: int = 0
    flags: int = 0
    divider: int = 0
    duration_ms: int = 0
    sample_clocks: int = 0
    produced: int = 0
    sent: int = 0
    gaps: int = 0
    ring_bytes: int = 0
    elapsed_ms: int = 0

    @property
    def state_name(self) -> str:
        return LOG_STATES[self.state] if self.state < len(LOG_STATES) else str(self.state)

    @property
    def sample_rate(self) -> float:
        return BASE_CLOCK_HZ / self.sample_clocks if self.sample_clocks else 0.0


@dataclass
class LogReadReq(Payload):
    """LOG_READ 0x0215 request (8 bytes): u32 max_bytes (whole words are returned, up to 64 KiB), u32 0. Answers at
    once with what the DDR ring holds (possibly nothing while the log runs). E_BAD_ARG detail 0 for max_bytes 0 or
    over 65536; E_STATE before any LOG_START."""

    FIELDS = [("max_bytes", "I"), ("_r", "4x")]
    max_bytes: int = LOG_READ_MAX


@dataclass
class LogChunkHeader(Payload):
    """LOG_READ response header (16 bytes; the data follow): 0 u32 seq (+1 per non-empty chunk), 4 u32 the FPGA
    word counter of the chunk's first word (modulo 2^32: words written since the start), 8 u32 bytes, 12 u8 flags
    (LOG_CHUNK_*), 13 u8 format, 14 u16 0. The data are whole ring words, oldest first; per word the enabled
    channels CH1 then CH2: format 0 the two samples in time order (core B, then core A), 1 the high byte, 2 a u16
    little-endian, 3 min then max."""

    FIELDS = [("seq", "I"), ("word", "I"), ("nbytes", "I"), ("flags", "B"), ("format", "B"), ("_r", "2x")]
    seq: int = 0
    word: int = 0
    nbytes: int = 0
    flags: int = 0
    format: int = 0

    @property
    def gap(self) -> bool:
        return bool(self.flags & LOG_CHUNK_GAP)

    @property
    def ended(self) -> bool:
        return bool(self.flags & LOG_CHUNK_ENDED)

    @property
    def running(self) -> bool:
        return bool(self.flags & LOG_CHUNK_RUNNING)


def parse_log_read(payload: bytes) -> tuple[LogChunkHeader, bytes]:
    h = LogChunkHeader.unpack(payload[:LOG_CHUNK_HEADER_BYTES])
    return h, bytes(payload[LOG_CHUNK_HEADER_BYTES:LOG_CHUNK_HEADER_BYTES + h.nbytes])


def log_channel_count(channels: int) -> int:
    return bin(channels & 3).count("1")


def log_sample_clocks(fmt: int, divider: int) -> int:
    """5 ns clocks per sample per channel: D + 1 for raw legacy samples (both cores at 200 MSa/s / (D + 1)), the
    filter window N = 2 (floor(D / 2) + 1) otherwise."""
    return divider + 1 if fmt == LOG_FMT_RAW8 else decim_n(divider)


def log_sample_rate(fmt: int, divider: int) -> float:
    return BASE_CLOCK_HZ / log_sample_clocks(fmt, divider)


def log_samples_per_word(fmt: int) -> int:
    """Samples per channel in one ring word: two in the legacy layout (cores B, A), one filtered."""
    return 2 if fmt == LOG_FMT_RAW8 else 1


def log_word_bytes(channels: int, fmt: int) -> int:
    """Bytes per ring word in a chunk."""
    return log_channel_count(channels) * log_samples_per_word(fmt) * LOG_SAMPLE_BYTES[fmt]


def log_bytes_per_s(channels: int, fmt: int, divider: int) -> float:
    return log_sample_rate(fmt, divider) * LOG_SAMPLE_BYTES[fmt] * log_channel_count(channels)


def log_check(channels: int, fmt: int, divider: int) -> tuple[int, str] | None:
    """LOG_START's argument checks that do not depend on the device state: (detail, text) or None."""
    if channels not in (1, 2, 3):
        return LOG_D_CHANNELS, "channels: bit 0 CH1, bit 1 CH2"
    if fmt > LOG_FMT_PEAK:
        return LOG_D_FORMAT, f"format {fmt}"
    if divider > LOG_MAX_DIVIDER:
        return LOG_D_DIVIDER, f"divider {divider} > 2^20 - 1"
    if divider == 0 and fmt != LOG_FMT_RAW8:
        return LOG_D_FORMAT, "the filter is not active at 200 MSa/s (divider 0)"
    if log_bytes_per_s(channels, fmt, divider) > LOG_MAX_BYTES_PER_S:
        return LOG_D_RATE, (f"rate {log_sample_rate(fmt, divider):.0f} S/s x {LOG_SAMPLE_BYTES[fmt]} B x "
                            f"{log_channel_count(channels)} ch is over the logger's {LOG_MAX_BYTES_PER_S} B/s")
    return None


#: GET_CAL byte 73 (protocol 1.10, bits 3 / 4 from 1.12)
CAL_LOADED = 0x01  # a calibration block is loaded
CAL_BOUND = 0x02  # bound to the current PWM centres (dc_calibration_offset[0..5])
#: bit 2 (the current divider is gated, fw 0.2.45-0.2.104) is reserved since fw 0.2.105: one calibration class
CAL_GAINS_MEASURED = 0x08  # 1.12: gains measured for this channel (gain_range != 0xFF)
CAL_GAINS_KEPT = 0x10  # 1.12: the gains were carried over from an earlier calibration
#: GET_CAL byte 256 track_flags (protocol 1.15, row 54)
TRACK_RECORD = 0x01  # a record was accepted since the reset (boot or a calibration)
TRACK_E = 0x02  # e identifiable at the last solve (spread of x >= 8 codes RMS)
TRACK_Q = 0x04  # q identifiable at the last solve
TRACK_LAST_USABLE = 0x08  # the last offered record was usable
TRACK_APPLIES = 0x10  # applies to the channel's next 8-bit readout (v3 bound, last arm legacy K = 0, any divider)
#: bits 5 / 6 (the class probe, fw 0.2.50-0.2.104) are reserved since fw 0.2.105
#: GET_CAL ages: none since the reset
CAL_AGE_NONE = 0xFFFFFFFF
#: v3 gain_range when the gains were never measured (gains 32768 = 1)
CAL_GAIN_NONE = 0xFF


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
    the v4 tracker (A - B = d + e x + q x^2 of the cores) from byte 256, and gain_a .. off_b then include it. Since
    fw 0.2.105 (FPGA 0x153D, the ADCs always at full rate) there is one calibration class: the stored block's class-1
    (gated) slots and the class probe's bytes 280..287 are reserved (0) and not decoded. (1.10-1.11 sent a 248-byte
    v2 layout,
    which this host no longer decodes: ``cal_version`` 2 is refused by the volts model.) Shorter answers parse with
    the missing tail zero; ``nbytes`` is the length received (0 for a locally built one)."""

    FIELDS = [("channel", "B"), ("_r0", "3x"), ("dc_calibration_offset", "7H"), ("_r1", "2x"),
              ("input_calibration", "7i"), ("adc_a_comp", "h"), ("adc_b_comp", "h"), ("dc_shift_center", "I"),
              ("dc_shift_size", "i"), ("dc_shift_value", "i"), ("adc_a_rawavg", "I"), ("adc_b_rawavg", "I"),
              ("cal_version", "B"), ("cal_flags", "B"), ("comp_a", "h"), ("comp_b", "h"), ("frac_a", "B"),
              ("frac_b", "B"), ("cal_zero_q8", "i"), ("corr16", "i"), ("cal_shift", "i"), ("gain_a", "I"),
              ("gain_b", "I"), ("off_a", "i"), ("off_b", "i"), ("gain16", "I"), ("pwm", "6H"), ("zero", "12h"),
              ("_rz", "24x"), ("slope", "12h"), ("_rs", "24x"), ("noise", "12B"), ("_rn", "12x"), ("gain", "2H"),
              ("_rg", "4x"), ("gain_range", "B"), ("gain_flags", "B"),
              ("gain_lever", "H"), ("track_flags", "B"), ("_r2", "x"), ("track_d_q8", "h"), ("track_e_q24", "i"),
              ("track_q_q24", "i"), ("track_age_ms", "I"), ("track_records", "I"), ("track_last_pairs", "H"),
              ("track_spread16", "H"), ("_r_cm", "8x"),
              ("prov_state", "B"), ("_r3", "3x"), ("prov", "56s"), ("prov_power_ons_since", "I"),
              ("prov_minutes_since", "I"), ("cap_trailer", "B"), ("cap_flags", "B"), ("cap_stored", "H"),
              ("cap_stored_fpga", "H"), ("cap_value", "H")]
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
    #: CAL_LOADED / CAL_BOUND / CAL_GAINS_MEASURED / CAL_GAINS_KEPT
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
    #: the stored v3 block (class 0, the one calibration class): PWM centres it was measured at, zero[core][range]
    #: (1/16 code, raw codes at dcoffset 0), slope[core][range] (Q16 codes per PWM count), noise[core][range] (RMS,
    #: 1/16 code), gain[core] (Q15, 32768 = 1), the hardware range the gains were measured on (CAL_GAIN_NONE =
    #: never), gain_flags (bit 0 carried over), gain_lever (span between the post levels, mean of both cores, 1/16 code)
    pwm: tuple[int, ...] = field(default_factory=lambda: (0,) * 6)
    zero: tuple[int, ...] = field(default_factory=lambda: (0,) * 12)
    slope: tuple[int, ...] = field(default_factory=lambda: (0,) * 12)
    noise: tuple[int, ...] = field(default_factory=lambda: (0,) * 12)
    gain: tuple[int, ...] = field(default_factory=lambda: (32768,) * 2)
    gain_range: int = CAL_GAIN_NONE
    gain_flags: int = 0
    gain_lever: int = 0
    #: 1.15 (row 54): TRACK_* bits; d (1/256 code), e and q (Q24) of A - B = d + e x + q x^2 (x = code - 128; d and e
    #: applied to the 8-bit readouts, q only reported); age (ms, CAL_AGE_NONE = none since the reset), accepted
    #: records, usable pairs of the last offered record, RMS spread of the accumulated x (1/16 code)
    track_flags: int = 0
    track_d_q8: int = 0
    track_e_q24: int = 0
    track_q_q24: int = 0
    track_age_ms: int = CAL_AGE_NONE
    track_records: int = 0
    track_last_pairs: int = 0
    track_spread16: int = 0
    #: 1.16 (row 55): calibration provenance: 0 none on the card, 1 valid, 2 present but unreadable; the stored
    #: 56 bytes (sector 708 bytes 432..487, parse_provenance); power-ons and operating minutes since (CAL_AGE_NONE =
    #: unknown)
    prov_state: int = 0
    prov: bytes = bytes(56)
    prov_power_ons_since: int = CAL_AGE_NONE
    prov_minutes_since: int = CAL_AGE_NONE
    #: 1.24 (row 64, 356..363): the stored FPGA 0x153A capture stages (sector 708 bytes 488..503): trailer state
    #: (CAP_TRAILER_STATES), flags (CAPF_HAS | CAPF_TRAINED | CAPF_OTHER), the stored 0x63 value and the FPGA version
    #: word it was trained on (0 unless valid), the 0x63 value in effect
    cap_trailer: int = 0
    cap_flags: int = 0
    cap_stored: int = 0
    cap_stored_fpga: int = 0
    cap_value: int = 0
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

    @property
    def has_capture(self) -> bool:
        """The answer carries the 1.24 capture stage fields (364 bytes)."""
        return self.nbytes >= 364

    def describe_capture(self) -> dict[str, Any] | None:
        """356..363 (protocol 1.24) in words; None for an older answer."""
        if not self.has_capture:
            return None
        f = self.cap_flags
        st = CAP_TRAILER_STATES.get(self.cap_trailer, f"state {self.cap_trailer}")
        out: dict[str, Any] = {"has_stages": bool(f & CAPF_HAS), "stored": st,
                               "stored_value": f"0x{self.cap_stored:04X}" if self.cap_trailer == 1 else None,
                               "stored_stages": cap_stages_text(self.cap_stored) if self.cap_trailer == 1 else None,
                               "stored_fpga": f"0x{self.cap_stored_fpga:04X}" if self.cap_trailer == 1 else None,
                               "in_effect": f"0x{self.cap_value:04X}" if f & CAPF_HAS else None,
                               "trained": bool(f & CAPF_TRAINED), "stored_for_other_bitstream": bool(f & CAPF_OTHER)}
        if self.cap_trailer == 1:
            text = (f"capture stages {out['stored_stages']} stored with the calibration (trained on FPGA "
                    f"0x{self.cap_stored_fpga:04X})")
        elif self.cap_trailer == 2:
            text = "a capture stage trailer is on the card but its CRC or version is wrong"
        else:
            text = "no capture stages stored with the calibration"
        if f & CAPF_HAS:
            if f & CAPF_TRAINED:
                text += "; in effect"
            elif f & CAPF_OTHER:
                text += (f"; not applied on this bitstream, {cap_stages_text(self.cap_value)} in effect: run the "
                         "calibration wizard")
            else:
                text += f"; {cap_stages_text(self.cap_value)} in effect (untrained)"
        out["text"] = text
        return out

    def provenance(self) -> "Provenance | None":
        """The stored provenance when prov_state is 1 (valid), else None."""
        if self.prov_state != 1:
            return None
        return parse_provenance(self.prov)

    @property
    def track_d(self) -> float:
        """A - B offset at code 128 the 8-bit readouts take out, codes."""
        return self.track_d_q8 / 256.0

    @property
    def track_e(self) -> float:
        """A - B slope (codes per code) the 8-bit readouts take out."""
        return self.track_e_q24 / 16777216.0

    @property
    def track_q(self) -> float:
        """The bow estimate (codes per code^2), reported, not applied."""
        return self.track_q_q24 / 16777216.0

    @staticmethod
    def _i(core: int, rng: int) -> int:
        return 6 * core + min(rng, 5)

    def zero_raw(self, core: int, rng: int) -> float:
        """Stored raw 0 V code of core (0 A, 1 B) at dcoffset 0, codes."""
        return self.zero[self._i(core, rng)] / 16.0

    def slope_codes(self, core: int, rng: int) -> float:
        """Stored offset slope, codes per PWM count."""
        return self.slope[self._i(core, rng)] / 65536.0

    def noise_rms(self, core: int, rng: int) -> float:
        return self.noise[self._i(core, rng)] / 16.0

    def zero16_at(self, core: int, rng: int, dcoffset: int) -> int:
        """z16 = zero + round(slope x dcoffset / 4096), 1/16 code (adc_cal.c acal_correction)."""
        i = self._i(core, rng)
        return self.zero[i] + _rdiv(self.slope[i] * dcoffset, 4096)

    def zero_at(self, core: int, rng: int, dcoffset: int) -> float:
        """Raw 0 V code of a core at ``dcoffset`` (raw uncompensated codes, the READ_RAW reference)."""
        return self.zero16_at(core, rng, dcoffset) / 16.0

    def gain_q16(self, core: int) -> int:
        """G_k in Q16 (acal_gain16; 65536 when the gains were never measured)."""
        return 65536 if self.gain_range == CAL_GAIN_NONE else 2 * self.gain[core]

    def gain_corr(self, core: int) -> float:
        """G_k: puts core k onto the channel's mean gain (the scale the volts calibration is fitted on)."""
        return self.gain_q16(core) / 65536.0

    def stored_gain16(self) -> int:
        """16-bit path gain (Q16): the harmonic mean of the gains of both cores, as the firmware rounds it."""
        a, b = self.gain_q16(0), self.gain_q16(1)
        return (2 * a * b + (a + b) // 2) // (a + b)


@dataclass
class AdcCalChannel:
    """One channel of the calibration v3 block (sector 708 bytes 128..431), decoded like GET_CAL bytes 112..255: the
    class-0 slots only (zero / slope / noise [core][range], gain [core]); the class-1 (gated) slots are ignored."""

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


#: GET_HEALTH 88 (protocol 1.19): how the firmware checks full-rate captures. Since fw 0.2.102 (one bitstream) the
#: mode is 2 on the built-for bitstream and 1 only when the boot 0x33 reported an unknown encode convention (the
#: firmware then judges the records by their data); 0 was the stock / EF2 / EP4 bitstreams up to fw 0.2.101.
ENC_MODES = {0: "none", 1: "record data", 2: "FPGA read 0x33"}
ENC_MODE_TEXT = {0: "no check (firmware before 0.2.102 on a stock, EF2 or EP4 bitstream)",
                 1: "record data: core B against its core A neighbours over the first 250 words (the boot 0x33 "
                    "reported an unknown encode convention; AL3 0x1532..0x1537 before fw 0.2.102)",
                 2: "FPGA self-check, read 0x33 after every capture"}
#: GET_HEALTH 90 flags (protocol 1.19)
ENC_READ = 0x01  # a 0x33 byte was read since boot
ENC_CONVENTION_BAD = 0x02  # 0x33 bits 3:2 at boot were not 01
ENC_LAST_BAD = 0x04  # the last checked capture was accepted garbled after 16 re-arms
#: retired: core order differed from the convention (0x1537, fw 0.2.62..0.2.101); never set since fw 0.2.102 (one
#: bitstream, the convention is the FPGA's own report), still decoded for older answers
ENC_ORDER_DIFFERS = 0x08
#: FPGA read 0x33 bits 7..4: the last pin check {CH2 A, CH2 B, CH1 A, CH1 B}, 1 = right; a good capture reads 0xF6
ENC33_PINS = ((7, "CH2 A"), (6, "CH2 B"), (5, "CH1 A"), (4, "CH1 B"))
ENC33_GOOD = 0xF6


def enc33_bad_pins(b: int) -> list[str]:
    """The ADC pins whose encode check failed in a 0x33 byte (bit 0 = 1 means the capture failed)."""
    return [name for bit, name in ENC33_PINS if not b >> bit & 1]


#: protocol 1.24 (fw 0.2.79, row 64): FPGA 0x153A capture stages. GET_HEALTH 106 / GET_CAL 357 flags
CAPF_HAS = 0x01  # the bitstream has the capture stages (0x153A+)
CAPF_TRAINED = 0x02  # the stored value for this bitstream is in effect (else the power-on 0x08E4 or an unsaved one)
CAPF_READ = 0x04  # outlier counts were read after a full-rate capture since boot
CAPF_FLAGGED = 0x08  # the last read flagged a core
CAPF_OTHER = 0x10  # a stored value exists for another bitstream (not applied: recalibrate)
CAPF_REPEAT = 0x20  # fw 0.2.81 (GET_HEALTH 106 only): a core flagged in 3 captures within a minute: recalibrate
#: fw 0.2.105 (GET_HEALTH 106 only): the runtime guard moved a flagged core to another stage of its trained clean set
CAPF_MOVED = 0x40
#: 0x63 at power-on: CH1 A 4, CH1 B 4, CH2 A 3, CH2 B 4, B cores by their own encode
CAP_POWER_ON = 0x08E4
#: the 0x36 order of the cores; flag masks use bit i = core i
CAP_CORES = ("CH1 A", "CH1 B", "CH2 A", "CH2 B")
#: self-check: a core is flagged when its outlier count exceeds its chip partner's by more than this
CAP_EXCESS_LIMIT = 8
#: GET_CAL 356: the stored value's trailer (sector 708 bytes 488..503)
CAP_TRAILER_STATES = {0: "none", 1: "valid", 2: "bad"}
CAP_TRAILER_MAGIC = b"ACAP"
CAP_TRAILER_OFFSET = 488
_CAP_TRAILER = struct.Struct("<4sHHHHI")


def cap_stage(v: int, core: int) -> int:
    """Stage (0..7) of core (CAP_CORES order) in a 0x63 value."""
    return (v >> (3 * core)) & 7


def cap_stages_text(v: int, ch: int | None = None) -> str:
    """'CH1 A4 B4, CH2 A3 B4' as the scope writes it (cs_text); ch 0 / 1 gives one channel. ' A-clk' follows a B core
    timed by its chip's A encode (0x63 bit 12 / 13)."""
    if ch is None:
        return f"{cap_stages_text(v, 0)}, {cap_stages_text(v, 1)}"
    t = f"CH{ch + 1} A{cap_stage(v, 2 * ch)} B{cap_stage(v, 2 * ch + 1)}"
    return t + (" A-clk" if v & (0x1000 << ch) else "")


def cap_core_names(mask: int) -> list[str]:
    return [n for i, n in enumerate(CAP_CORES) if mask >> i & 1]


def cap_excess(counts: Iterable[int]) -> list[int]:
    """Per core its outlier count minus its chip partner's (0 when below), as cs_excess."""
    c = list(counts)
    return [max(0, c[i] - c[i ^ 1]) for i in range(4)]


def cap_flagged(counts: Iterable[int]) -> int:
    """The cores whose excess is above CAP_EXCESS_LIMIT, as a mask (cs_flagged)."""
    return sum(1 << i for i, e in enumerate(cap_excess(counts)) if e > CAP_EXCESS_LIMIT)


def pack_cap_trailer(value: int, fpga: int) -> bytes:
    """Sector 708 bytes 488..503: u32 'ACAP', u16 version 1, u16 length 16, u16 the 0x63 value, u16 the FPGA version
    word it was trained on, u32 zlib.crc32 of the 12 bytes before (cs_trailer_pack)."""
    head = struct.pack("<4sHHHH", CAP_TRAILER_MAGIC, 1, 16, value, fpga)
    return head + struct.pack("<I", zlib.crc32(head))


def parse_cap_trailer(b: bytes) -> tuple[int, int, int]:
    """(state, value, fpga) of a stored trailer (cs_trailer_unpack): state 0 none (no magic), 1 valid, 2 bad (magic
    present, version, length or CRC wrong); value and fpga are 0 unless valid."""
    if len(b) < 16 or b[:4] != CAP_TRAILER_MAGIC:
        return 0, 0, 0
    _m, ver, length, v, fpga, crc = _CAP_TRAILER.unpack_from(b, 0)
    if ver != 1 or length != 16 or crc != zlib.crc32(b[:12]):
        return 2, 0, 0
    return 1, v, fpga


#: callers of event types 21 / 22 (b >> 12, protocol rows 57 / 58)
FPGA_CALLERS = {0: "UI", 1: "host", 2: "calibration wizard", 3: "v4 class probe (retired in fw 0.2.105)", 4: "check"}
#: event type 21 reasons (a >> 5 & 3, protocol 1.19)
ENC_REASONS = {0: "check after a rate write", 1: "record data check", 2: "FPGA 0x33", 3: "boot convention check"}


@dataclass
class HealthResp(Payload):
    """GET_HEALTH response, 96 bytes (protocol 1.16, row 55), 104 bytes from 1.19 (row 58: the full-rate encode check
    in 88..103; a 96-byte answer reads them as 0 and ``has_enc_check`` is False), 124 bytes and layout 2 from 1.24
    (row 64: the FPGA 0x153A capture stages in 104..123; a shorter answer reads them as 0 and ``has_capture`` is
    False), 128 bytes and layout 3 from 1.25 (fw 0.2.102: 124 u16 ``fw_fpga_version``, 126 u8 ``fpga_flags`` as
    GET_INFO 76..78, 127 0; a shorter answer reads them as 0)."""

    FIELDS = [("layout", "B"), ("_r0", "x"), ("length", "H"), ("usb_state", "B"), ("usb_flags", "B"),
              ("usb_power", "B"), ("usb_faddr", "B"), ("suspend_ms", "I"), ("since_config_ms", "I"), ("cp15", "I"),
              ("power_on", "I"), ("minutes", "I"), ("uptime_ms", "I"), ("life_flags", "B"), ("last_end", "B"),
              ("settings_flags", "B"), ("saves", "B"), ("last_save_result", "i"), ("settings_eval_ms", "I"),
              ("rec_flags", "B"), ("rec_type", "B"), ("rec_restarts", "H"), ("rec_power_on", "I"),
              ("rec_exc_lr", "I"), ("rec_spsr", "I"), ("rec_exc_sp", "I"), ("rec_reset_lr", "I"),
              ("rec_reset_sp", "I"), ("rec_reset_cpsr", "I"), ("rec_fsr", "I"), ("rec_far", "I"),
              ("rec_minutes", "I"), ("enc_mode", "B"), ("enc_last33", "B"), ("enc_flags", "B"),
              ("enc_boot33", "B"), ("enc_bad", "I"), ("enc_recovered", "I"), ("enc_checked", "I"),
              ("cap_value", "H"), ("cap_flags", "B"), ("cap_flagged", "B"), ("cap_counts", "4H"), ("cap_reads", "I"),
              ("cap_flagged_n", "I"), ("fw_fpga_version", "H"), ("fpga_flags", "B"), ("_r127", "x")]
    layout: int = 3
    length: int = 128
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
    #: protocol 1.19 (bytes 88..103): check mode, the last 0x33 byte, flags (ENC_*), 0x33 at boot, captures found
    #: garbled (= link counter 62), of those recovered by a re-arm (= 63), full-rate captures checked since boot
    enc_mode: int = 0
    enc_last33: int = 0
    enc_flags: int = 0
    enc_boot33: int = 0
    enc_bad: int = 0
    enc_recovered: int = 0
    enc_checked: int = 0
    #: protocol 1.24 (bytes 104..123): the 0x63 value in effect (0 without the stages), flags (CAPF_*), the cores
    #: flagged by the last read (bit i = CAP_CORES[i]), the last 0x36 outlier counts (CH1 A, CH1 B, CH2 A, CH2 B),
    #: full-rate captures whose counts were read, of those with a flagged core
    cap_value: int = 0
    cap_flags: int = 0
    cap_flagged: int = 0
    cap_counts: tuple[int, ...] = (0, 0, 0, 0)
    cap_reads: int = 0
    cap_flagged_n: int = 0
    #: protocol 1.25 (bytes 124..126): the bitstream the firmware is built for and the FPGA_F_* flags (as GET_INFO
    #: 76..78; 0 in a shorter answer)
    fw_fpga_version: int = 0
    fpga_flags: int = 0

    @property
    def has_enc_check(self) -> bool:
        return self.length >= 104

    @property
    def has_fpga_version(self) -> bool:
        """The answer carries the 1.25 bitstream block (layout 3, 128 bytes)."""
        return self.layout >= 3 and self.length >= 128

    @property
    def fpga_mismatch(self) -> bool:
        """1.25: the scope does not run the bitstream the firmware is built for (GET_HEALTH 126 bit 0)."""
        return bool(self.fpga_flags & FPGA_F_MISMATCH)

    def describe_fpga(self) -> dict[str, Any] | None:
        """Bytes 124..126 (protocol 1.25) in words; None for an older answer."""
        if not self.has_fpga_version:
            return None
        text = (f"FPGA bitstream mismatch: this firmware needs 0x{self.fw_fpga_version:04X}; captures and settings "
                "are refused until it is flashed" if self.fpga_mismatch else
                f"FPGA bitstream 0x{self.fw_fpga_version:04X}, the one this firmware is built for")
        return {"firmware_bitstream": f"0x{self.fw_fpga_version:04X}", "mismatch": self.fpga_mismatch, "text": text}

    @property
    def has_capture(self) -> bool:
        """The answer carries the 1.24 capture stage block (layout 2, 124 bytes)."""
        return self.layout >= 2 and self.length >= 124

    def describe_capture(self) -> dict[str, Any] | None:
        """Bytes 104..123 (protocol 1.24) in words; None for an older answer."""
        if not self.has_capture:
            return None
        f = self.cap_flags
        if not f & CAPF_HAS:
            return {"has_stages": False, "text": "no ADC capture stages on this bitstream (FPGA 0x153A and later)"}
        v = self.cap_value
        if f & CAPF_TRAINED:
            source = "trained"
        elif v == CAP_POWER_ON:
            source = "power-on default"
        else:
            source = "not stored (a training not saved yet)"
        read = bool(f & CAPF_READ)
        counts = dict(zip(CAP_CORES, self.cap_counts)) if read else None
        flagged = cap_core_names(self.cap_flagged) if read else []
        out: dict[str, Any] = {
            "has_stages": True, "value": f"0x{v:04X}", "stages": cap_stages_text(v), "source": source,
            "stored_for_other_bitstream": bool(f & CAPF_OTHER), "last_counts": counts, "flagged_cores": flagged,
            "captures_read": self.cap_reads, "captures_flagged": self.cap_flagged_n,
            "flagged_repeatedly": bool(f & CAPF_REPEAT), "moved_by_guard": bool(f & CAPF_MOVED)}
        if not read:
            state = "no 200 MSa/s capture read yet"
        elif f & CAPF_FLAGGED:
            state = ", ".join(f"{n}: {self.cap_counts[CAP_CORES.index(n)]} outliers" for n in flagged)
        else:
            state = "capture clean"
        text = f"capture stages {out['stages']} ({source}); {state}"
        if self.cap_flagged_n:
            text += f"; {self.cap_flagged_n} of {self.cap_reads} full-rate captures flagged since boot"
        if f & CAPF_OTHER:
            text += "; a value stored for another bitstream is not applied: run the calibration wizard"
        if f & CAPF_REPEAT:
            text += "; a core was flagged in 3 captures within a minute: run the calibration wizard"
        if f & CAPF_MOVED:
            text += "; a flagged core was moved to another trained stage this boot (stored)"
        out["text"] = text
        return out

    def describe_enc(self) -> dict[str, Any] | None:
        """Bytes 88..103 (protocol 1.19) in words; None for a 1.16..1.18 answer (96 bytes)."""
        if not self.has_enc_check:
            return None
        f = self.enc_flags
        read = bool(f & ENC_READ)
        out: dict[str, Any] = {
            "mode": ENC_MODES.get(self.enc_mode, f"mode {self.enc_mode}"),
            "mode_text": ENC_MODE_TEXT.get(self.enc_mode, ""),
            "last_33": f"0x{self.enc_last33:02X}" if read else None,
            "last_33_bad_pins": enc33_bad_pins(self.enc_last33) if read and self.enc_last33 & 1 else [],
            "boot_33": f"0x{self.enc_boot33:02X}" if self.enc_mode == 2 else None,
            "convention_bad": bool(f & ENC_CONVENTION_BAD), "last_capture_accepted_bad": bool(f & ENC_LAST_BAD),
            "core_order_differs": bool(f & ENC_ORDER_DIFFERS),
            "captures_checked": self.enc_checked, "captures_bad": self.enc_bad,
            "captures_recovered": self.enc_recovered,
            "captures_accepted_bad_or_abandoned": max(0, self.enc_bad - self.enc_recovered)}
        if self.enc_mode == 0:
            text = "full-rate captures are not checked on this bitstream"
        elif f & ENC_CONVENTION_BAD:
            text = f"unknown encode convention, 0x33 = 0x{self.enc_boot33:02X} at boot"
        elif self.enc_checked == 0:
            text = "encode check, no 200 MSa/s capture yet"
        elif f & ENC_LAST_BAD:
            pins = ", ".join(out["last_33_bad_pins"]) or "a core"
            text = (f"the last full-rate capture was accepted garbled after 16 re-arms ({pins}): treat 200 MSa/s data "
                    "since then as suspect")
        elif read and self.enc_last33 & 1:
            text = f"encode check failed on {', '.join(out['last_33_bad_pins']) or 'a pin'}"
        else:
            text = "encode check OK" + (f", {self.enc_bad} captures re-taken" if self.enc_bad else "")
        if f & ENC_ORDER_DIFFERS:
            text += "; ADC core order differs from the convention: recalibrate after flashing 0x153B"
        out["text"] = text
        return out

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
        return {"usb": usb, "cpu": cpu, "life": life, "settings": settings, "restart_record": rec,
                "encode_check": self.describe_enc(), "capture_stages": self.describe_capture(),
                "fpga": self.describe_fpga()}


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
_ADC_CAL_CH = struct.Struct("<6H12h24x12h24x12B12x2H4xBBH")


def parse_adc_cal_block(block: bytes) -> list[AdcCalChannel]:
    """The v3 block: u32 'ACAL', u16 version 3, u16 length 304, u16 classes (1 from fw 0.2.105; 2 from fw
    0.2.45-0.2.104, whose class-1 gated slots are ignored), u16 0, two channels of 144 bytes, u32 zlib.crc32 of the
    300 bytes before it. Raises ValueError when the header or the CRC is wrong."""
    if len(block) < 304 or block[:4] != ADC_CAL_MAGIC:
        raise ValueError("not a calibration block")
    ver, length, classes, _r = struct.unpack_from("<HHHH", block, 4)
    if ver != 3 or length != 304 or classes not in (1, 2):
        raise ValueError(f"calibration block version {ver}, length {length}, classes {classes} (host decodes v3)")
    (crc,) = struct.unpack_from("<I", block, 300)
    if crc != zlib.crc32(block[:300]):
        raise ValueError("calibration block CRC mismatch")
    out = []
    for i in range(2):
        v = _ADC_CAL_CH.unpack_from(block, 12 + 144 * i)
        out.append(AdcCalChannel(v[:6], v[6:18], v[18:30], v[30:42], v[42:44], v[44], v[45], v[46]))
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
    "usb_inferred_resumes", "usb_soft_reconnects",
    # fw 0.2.61 (64 counters, protocol 1.18 row 57): full-rate capture checks that found a garbled ADC core, and of
    # those recovered by the retries; per capture since fw 0.2.62 (1.19 row 58: = GET_HEALTH 92 / 96)
    "adc_fullrate_bad", "adc_fullrate_recovered",
    # fw 0.2.72 (68 counters, protocol 1.22): FPGA writes that set the capture state, counted (event type 22 lists them
    # only at trace level 1, GET_EVENT_LOG flags bits 2:1): 0x0D rate, 0x53 / 0x56 filter, 0x01 = 1 sampling resets;
    # then the full-rate captures on FPGA 0x1538+ judged by their data instead of read 0x33 because such a write came
    # after their sampling reset (0x33 bit 0 is sticky until the next reset)
    "fpga_rate_writes", "fpga_filter_writes", "fpga_sampling_resets", "adc_fullrate_by_data",
    # fw 0.2.78 (70 counters, protocol 1.23 row 63): requests that closed the scope's modal screens (menus, dialogs) to
    # run, and E_BUSY answers instead (a calibration measurement, a file save, USB drive mode, or the 1.5 s time-out)
    "ui_yields", "ui_yield_refused")

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
#: 1.22 (fw 0.2.72) bits 2:1: the FPGA write trace (event type 22, every 0x0D / 0x53 / 0x56 / 0x01 = 1 write with its
#: caller) on or off; it is off after boot (the writes are counted in GET_LINK_STATS 64-66 either way); both bits = E_BAD_ARG
EVENT_LOG_FPGA_TRACE_OFF = 0x02
EVENT_LOG_FPGA_TRACE_ON = 0x04

#: GET_EVENT_LOG entry types (protocol 1.5, section 14 row 42)
EVENT_TYPES = {1: "bus_reset", 2: "suspend", 3: "resume", 4: "disconnect", 5: "ep0_setup", 6: "ep0_short_packet",
               7: "set_configuration", 8: "clear_halt", 9: "dtr", 10: "rx_ring_full", 11: "rx_release",
               12: "tx_flushed", 13: "tx_aborted", 14: "usb_irq_polled", 15: "restart", 16: "usb_init_state",
               17: "tx_abort_txcsr", 18: "tx_abort_frame", 19: "usb_postinit_state", 20: "usb_soft_disconnect",
               21: "adc_fullrate_garbled", 22: "fpga_write", 23: "adc_capture"}
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
        if t == 21:  # 1.19: a = reason << 5 | re-arms (bit 7 not recovered), b = caller << 12 | value
            reason, value = (a >> 5) & 3, b & 0xFFF
            who = FPGA_CALLERS.get(b >> 12, f"caller {b >> 12}")
            if reason == 3:
                return f"adc_fullrate_garbled boot convention check: 0x33 = 0x{value:02X} (bits 3:2 != 01)"
            if reason == 2:
                what = f"0x33 = 0x{value:02X} ({', '.join(enc33_bad_pins(value)) or 'no pin'} wrong)"
            elif reason == 1:
                what = f"residual {value / 16:.2f} codes"
            else:
                what = f"value {value}"
            n = a & 0x1F
            end = "NOT recovered (accepted garbled)" if a & 0x80 else "recovered"
            return (f"adc_fullrate_garbled ({ENC_REASONS[reason]}, {who}): {what}, {n}{'+' if n == 31 else ''} "
                    f"re-arm(s), {end}")
        if t == 22:  # 1.18: an FPGA write of 0x0D, 0x53, 0x56 or 0x01 = 1; b = caller << 12 | value (12 bits)
            who = FPGA_CALLERS.get(b >> 12, f"caller {b >> 12}")
            return f"fpga_write 0x{a:02X} = {b & 0xFFF}{'+' if b & 0xFFF == 0xFFF else ''} by {who}"
        if t == 23:  # 1.24: FPGA 0x153A capture stages; a bit 4 = a training ended
            cores = ", ".join(cap_core_names(a & 0x0F))
            if a & 0x10:
                end = f"no clean stage for {cores}, kept" if a & 0x0F else "now"
                return f"adc_capture training ended: {end} {cap_stages_text(b)} (0x{b:04X})"
            if a & 0x20:  # fw 0.2.105: the runtime guard moved these cores inside their trained clean set (stored)
                return f"adc_capture guard moved {cores}: now {cap_stages_text(b)} (0x{b:04X})"
            # fw 0.2.81: logged once per core when it was flagged in 3 captures within a minute (0.2.79-0.2.80: when
            # the set of flagged cores changed)
            return f"adc_capture self-check flagged {cores or 'no core'}: {b} outliers beyond the chip partner"
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
    #: 1.22 (fw 0.2.72): the FPGA write trace (event type 22) is on (word at byte 2, bit 0)
    fpga_trace: bool = False


def _event(payload: bytes, off: int) -> DeviceEvent:
    return DeviceEvent(*struct.unpack_from("<IBBH", payload, off))


def parse_event_log(payload: bytes) -> EventLog:
    """GET_EVENT_LOG: u16 n (<= 64, <= 128 from fw 0.2.15), u16 0, u32 total, u32 now_ms, then n x (u32 ms, u8 type,
    u8 a, u16 b), oldest first. fw 0.2.15 appends a reset snapshot: u16 m (<= 16), u16 frozen, u32 reset_ms, m
    entries (the events right before the first bus reset since the previous read). fw 0.2.24 then appends the
    bus-level ring: u16 k (<= 16), u16 0, u32 bus events since boot, k entries. Older firmware: exactly 12 + 8n."""
    if len(payload) < 12:
        return EventLog(0, 0, [])
    n, flags, total, now = struct.unpack_from("<HHII", payload)
    n = min(n, (len(payload) - 12) // 8)
    ev = [_event(payload, 12 + 8 * i) for i in range(n)]
    log = EventLog(total, now, ev, fpga_trace=bool(flags & 1))
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
    out = struct.pack("<HHII", len(log.events), int(log.fpga_trace), log.total & 0xFFFFFFFF, log.now_ms & 0xFFFFFFFF) + \
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
    Cmd.READ_RAW: ReadRawReq.size(), Cmd.READ_TRACE: ReadTraceReq.size(),
    Cmd.ACQUIRE_AVG: AcquireAvgReq.size(), Cmd.GET_GEN: 0, Cmd.SET_GEN: SetGenReq.size(), Cmd.GET_CAL: 1,
    Cmd.READ_SCREEN: ReadScreenReq.size(), Cmd.ENTER_FEL: 4, Cmd.SET_ACQ_STATE: SetAcqStateReq.size(),
    Cmd.GET_CAPTURE_INFO: 0, Cmd.GET_EVENT_LOG: 0, Cmd.SD_INFO: 4, Cmd.SD_READ_SECTORS: 8, Cmd.SD_STAGE: 8, Cmd.SD_WRITE_SECTORS: 16,
    Cmd.READ_SPECTRUM: ReadSpectrumReq.size(), Cmd.GET_DECIM: 0, Cmd.SET_DECIM: SetDecimReq.size(),
    Cmd.GET_HEALTH: 0, Cmd.SAVE_REPORT: 0, Cmd.SET_SEQ: SetSeqReq.size(), Cmd.GET_SEQ: 0,
    Cmd.READ_SEQ: ReadSeqReq.size(), Cmd.BODE_CTRL: BodeCtrlReq.size(), Cmd.READ_BODE: ReadBodeReq.size(),
    Cmd.CAL_PROBE: CalProbeReq.size(), Cmd.GET_COUNTER: CounterReq.size(), Cmd.SET_TRIGGER2: Trigger2.size(),
    Cmd.LOG_START: LogStartReq.size(), Cmd.LOG_READ: LogReadReq.size(), Cmd.LOG_STOP: 0,
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
