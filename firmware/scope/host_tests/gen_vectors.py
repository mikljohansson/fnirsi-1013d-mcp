#!/usr/bin/env python3
"""Generate docs/protocol-vectors.json: protocol v1.0 frame test vectors (docs/protocol.md section 4).

Frames are built here with the Python standard library CRCs only (binascii.crc_hqx for the header,
zlib.crc32 for the payload), independently of the firmware C code. The same file is consumed by
firmware/scope/host_tests/check_vectors.py (C parser cross-check) and by the MCP server tests.

Deterministic: running it twice gives the same file. Usage:
    python3 gen_vectors.py [-o ../../../docs/protocol-vectors.json]
"""

import argparse
import binascii
import json
import os
import struct
import zlib

MAGIC = b"\xa5\x13"
FLAG_RESPONSE = 0x01
MAX_REQUEST_PAYLOAD = 492

CMDS = {
    "PING": 0x0001, "GET_INFO": 0x0002, "GET_STATUS": 0x0003, "SET_REMOTE": 0x0004,
    "GET_BATTERY": 0x0005, "TEST_PATTERN": 0x0006,
    "GET_CHANNEL": 0x0100, "SET_CHANNEL": 0x0101, "GET_TIMEBASE": 0x0102, "SET_TIMEBASE": 0x0103,
    "GET_TRIGGER": 0x0104, "SET_TRIGGER": 0x0105,
    "ARM": 0x0200, "FORCE": 0x0201, "STOP": 0x0202, "WAIT": 0x0203, "READ_RAW": 0x0204,
    "READ_TRACE": 0x0205, "READ_LIVE": 0x0206, "ACQUIRE_AVG": 0x0207,
    "GET_GEN": 0x0300, "SET_GEN": 0x0301, "GET_CAL": 0x0400, "READ_SCREEN": 0x0401,
    "DEBUG_FPGA": 0x0F00, "ENTER_FEL": 0x0F01,
}


def header(cmd, seq, status=0, flags=0, length=0, major=1):
    h = MAGIC + struct.pack("<BBHHHI", major, flags, cmd, seq, status, length)
    return h + struct.pack("<H", binascii.crc_hqx(h, 0xFFFF))


def frame(cmd, seq, payload=b"", status=0, flags=0, major=1):
    f = header(cmd, seq, status, flags, len(payload), major)
    if payload:
        f += payload + struct.pack("<I", zlib.crc32(payload) & 0xFFFFFFFF)
    return f


def decoded(cmd, seq, payload=b"", status=0, flags=0):
    return {"cmd": cmd, "seq": seq, "status": status, "flags": flags, "len": len(payload),
            "payload": payload.hex()}


def ev_frame(cmd, seq, payload=b"", status=0, flags=0):
    d = decoded(cmd, seq, payload, status, flags)
    d["event"] = "frame"
    return d


# Request payloads, one per command (field layouts: docs/protocol.md section 8)
REQUESTS = [
    ("PING", b"hi", "echo 2 bytes (spec section 13 example, seq 2)"),
    ("GET_INFO", b"", "spec section 13 example, seq 1"),
    ("GET_STATUS", b"", ""),
    ("SET_REMOTE", struct.pack("<BBH", 1, 0x00, 15), "acquire, flags 0, watchdog 15 s"),
    ("GET_BATTERY", b"", ""),
    ("TEST_PATTERN", struct.pack("<IB3x", 4076, 0), "4076 byte counter pattern: response frame is exactly 4096 bytes (ZLP case, test T1)"),
    ("GET_CHANNEL", struct.pack("<B", 1), "CH2"),
    ("SET_CHANNEL", struct.pack("<BBBBBBBBi", 0, 0x3F, 1, 5, 0, 0, 1, 0, -20),
     "CH1 all fields: enable, 100 mV/div (range 5), DC, no invert, 1x probe, UI dcoffset -20"),
    ("GET_TIMEBASE", b"", ""),
    ("SET_TIMEBASE", struct.pack("<B3xI", 0, 19), "UI index 19 (spec section 13 example, seq 3)"),
    ("GET_TRIGGER", b"", ""),
    ("SET_TRIGGER", struct.pack("<HBBBBHIIII", 0x00BF, 1, 1, 140, 1, 0, 2000, 16000, 0, 0),
     "CH2, falling, level 140, normal, pretrigger 2000, total 16000, auto_timeout default"),
    ("ARM", struct.pack("<I", 0), ""),
    ("FORCE", b"", ""),
    ("STOP", b"", ""),
    ("WAIT", struct.pack("<I", 1000), "timeout 1000 ms"),
    ("READ_RAW", struct.pack("<BBBBiII", 0x0F, 1, 0, 0, 0, 8192, 0), "all four cores, relative to capture start, per-core blocks, 8192 words"),
    ("READ_TRACE", struct.pack("<BBHII", 0x03, 0x00, 0, 0, 0), "both channels, everything"),
    ("READ_LIVE", struct.pack("<BBH", 0, 0, 100), "CH1 A, 100 bytes"),
    ("ACQUIRE_AVG", struct.pack("<HBBIIII", 64, 0x01, 0x01, 100, 0, 2000, 0), "64 captures, CH1, require trigger"),
    ("GET_GEN", b"", ""),
    ("SET_GEN", struct.pack("<BBBBQII", 0x07, 1, 0, 0, 10_000_000, 5000, 0), "enable, 10 kHz (10,000,000 mHz), 50.00 %"),
    ("GET_CAL", struct.pack("<B", 0), "CH1"),
    ("READ_SCREEN", struct.pack("<HH", 0, 40), "rows 0..39"),
    ("DEBUG_FPGA", bytes([1, 1, 0x06, 3, 2]), "write command 0x06, read 2 bytes (FPGA version)"),
    ("ENTER_FEL", struct.pack("<I", 0x4C45465F), "magic 'FEL_' little-endian"),
]


def build():
    vectors = []

    def add(name, data, device_events, description="", frames=None):
        vectors.append({
            "name": name,
            "description": description,
            "hex": data.hex(),
            # Valid frames a direction-agnostic parser finds (host side: responses are valid too)
            "frames": frames if frames is not None else [],
            # Events the scope firmware's parser produces, in order (see top-level "device_events")
            "device_events": device_events,
        })

    # 1. one request per command
    seq = 10
    for name, payload, desc in REQUESTS:
        cmd = CMDS[name]
        s = {"PING": 2, "GET_INFO": 1, "SET_TIMEBASE": 3}.get(name, seq)
        f = frame(cmd, s, payload)
        add("req_" + name.lower(), f, [ev_frame(cmd, s, payload)], desc, [decoded(cmd, s, payload)])
        seq += 1

    # 2. spec section 13 responses
    f = frame(CMDS["PING"], 2, b"hi", flags=FLAG_RESPONSE)
    add("resp_ping", f, [], "PING response (spec section 13). The device parser drops frames with RESPONSE set",
        [decoded(CMDS["PING"], 2, b"hi", flags=FLAG_RESPONSE)])
    f = frame(CMDS["SET_CHANNEL"], 4, b"", status=6, flags=FLAG_RESPONSE)
    add("resp_set_channel_not_remote", f, [], "SET_CHANNEL response E_NOT_REMOTE (spec section 13)",
        [decoded(CMDS["SET_CHANNEL"], 4, b"", status=6, flags=FLAG_RESPONSE)])
    err_payload = struct.pack("<H", 1) + b"range 7 > 6"
    f = frame(CMDS["SET_CHANNEL"], 5, err_payload, status=4, flags=FLAG_RESPONSE)
    add("resp_error_with_text", f, [], "E_BAD_ARG with detail 1 and text (section 4.2)",
        [decoded(CMDS["SET_CHANNEL"], 5, err_payload, status=4, flags=FLAG_RESPONSE)])

    # 3. sizes
    p = bytes(i & 0xFF for i in range(MAX_REQUEST_PAYLOAD))
    f = frame(CMDS["PING"], 100, p)
    assert len(f) == 512
    add("req_ping_max_payload", f, [ev_frame(CMDS["PING"], 100, p)], "492-byte payload: the largest request frame, 512 bytes",
        [decoded(CMDS["PING"], 100, p)])
    p1 = bytes((i * 7) & 0xFF for i in range(MAX_REQUEST_PAYLOAD + 1))
    f = frame(CMDS["PING"], 101, p1)
    add("req_oversize_493", f, [{"event": "bad_len", "cmd": CMDS["PING"], "seq": 101}],
        "len 493 > 492 with a valid header CRC: device answers E_BAD_LEN at the header and discards the 493 + 4 following bytes",
        [decoded(CMDS["PING"], 101, p1)])
    f2 = frame(CMDS["GET_STATUS"], 103)
    add("req_oversize_then_frame", f + f2,
        [{"event": "bad_len", "cmd": CMDS["PING"], "seq": 101}, ev_frame(CMDS["GET_STATUS"], 103)],
        "oversize frame followed directly by a valid one: the skip ends exactly after the oversize frame's CRC",
        [decoded(CMDS["PING"], 101, p1), decoded(CMDS["GET_STATUS"], 103)])
    f = frame(CMDS["PING"], 102, b"\x00")
    add("req_ping_1_byte", f, [ev_frame(CMDS["PING"], 102, b"\x00")], "", [decoded(CMDS["PING"], 102, b"\x00")])
    f = frame(CMDS["GET_STATUS"], 0xFFFF, b"")
    add("req_seq_ffff", f, [ev_frame(CMDS["GET_STATUS"], 0xFFFF)], "sequence number at the wrap", [decoded(CMDS["GET_STATUS"], 0xFFFF)])

    # 4. corruption
    good = frame(CMDS["GET_INFO"], 1)
    bad = bytearray(good); bad[15] ^= 0x01
    add("bad_header_crc", bytes(bad), [], "header CRC wrong: silently dropped, no answer")
    bad = bytearray(frame(CMDS["PING"], 2, b"hi")); bad[16] ^= 0x20
    add("bad_payload_data", bytes(bad), [{"event": "bad_crc", "cmd": CMDS["PING"], "seq": 2}],
        "payload byte flipped: device answers E_BAD_CRC with the request cmd/seq")
    bad = bytearray(frame(CMDS["PING"], 2, b"hi")); bad[-1] ^= 0x80
    add("bad_payload_crc_field", bytes(bad), [{"event": "bad_crc", "cmd": CMDS["PING"], "seq": 2}], "payload CRC field flipped")
    bad = bytearray(good); bad[1] = 0x14
    add("bad_magic", bytes(bad), [], "second magic byte wrong (0x14)")
    bad = bytearray(good); bad[0] = 0x5A
    add("bad_magic0", bytes(bad), [], "first magic byte wrong")
    f = frame(CMDS["GET_INFO"], 7, major=2)
    add("bad_version", f, [{"event": "bad_version", "cmd": CMDS["GET_INFO"], "seq": 7}],
        "proto_major 2: device answers E_VERSION", [])
    add("truncated_header", good[:10], [], "10 of 16 header bytes, then silence: discarded after 100 ms")
    f = frame(CMDS["PING"], 3, b"hello")
    add("truncated_payload", f[:-3], [], "header and part of payload/CRC, then silence: discarded after 100 ms")
    f = frame(CMDS["PING"], 4, b"x", flags=FLAG_RESPONSE)
    add("response_flag_to_device", f, [], "valid frame with RESPONSE set sent to the device: dropped",
        [decoded(CMDS["PING"], 4, b"x", flags=FLAG_RESPONSE)])

    # 5. stream behaviour
    f = frame(CMDS["GET_STATUS"], 20)
    add("junk_then_frame", b"AT\r\n\x00\xff\x13\xa5" + f, [ev_frame(CMDS["GET_STATUS"], 20)],
        "ModemManager style junk and a stray 0xA5 before a frame", [decoded(CMDS["GET_STATUS"], 20)])
    f1 = frame(CMDS["PING"], 21, b"abc"); f2 = frame(CMDS["GET_INFO"], 22)
    add("two_frames_back_to_back", f1 + f2, [ev_frame(CMDS["PING"], 21, b"abc"), ev_frame(CMDS["GET_INFO"], 22)],
        "one USB packet holding two frames", [decoded(CMDS["PING"], 21, b"abc"), decoded(CMDS["GET_INFO"], 22)])
    f = frame(CMDS["GET_INFO"], 23)
    add("resync_after_false_magic", MAGIC + f, [ev_frame(CMDS["GET_INFO"], 23)],
        "a5 13 then a real frame: the false header fails its CRC and parsing restarts after the bad magic",
        [decoded(CMDS["GET_INFO"], 23)])
    f = frame(CMDS["PING"], 24, MAGIC + b"\x01\x00")
    add("magic_inside_payload", f, [ev_frame(CMDS["PING"], 24, MAGIC + b"\x01\x00")],
        "payload containing the magic bytes is not a frame start", [decoded(CMDS["PING"], 24, MAGIC + b"\x01\x00")])

    # 6. ASCII probe (section 10)
    add("ascii_ver", b"#VER\n", [{"event": "ascii", "line": "VER"}], "#VER probe")
    add("ascii_local_crlf", b"#LOCAL\r\n", [{"event": "ascii", "line": "LOCAL"}], "#LOCAL with CR LF (the LF is junk)")
    add("ascii_overflow", b"#" + b"X" * 70 + b"\n", [{"event": "ascii_overflow"}], "line over 63 bytes: #ERR,UNKNOWN")
    add("ascii_then_frame", b"#VER\r" + frame(CMDS["PING"], 25, b"z"),
        [{"event": "ascii", "line": "VER"}, ev_frame(CMDS["PING"], 25, b"z")], "", [decoded(CMDS["PING"], 25, b"z")])

    return {
        "format": "fnirsi-1013d protocol test vectors",
        "version": 1,
        "protocol": "1.0",
        "spec": "docs/protocol.md",
        "generator": "firmware/scope/host_tests/gen_vectors.py",
        "notes": [
            "hex: the byte stream fed to a parser in one go, followed by >= 100 ms of silence.",
            "frames: structurally valid frames (magic a5 13, header CRC-16/CCITT-FALSE, payload CRC-32/ISO-HDLC, "
            "proto_major 1) a direction-agnostic parser finds, responses included. The 492-byte request payload "
            "limit is a device rule and shows up only in device_events.",
            "device_events: what the scope parser reports, in order. event = frame | bad_crc | bad_len | "
            "bad_version | ascii | ascii_overflow. Dropped input produces no event. The device ignores frames "
            "with the RESPONSE flag.",
            "Field values: little-endian; payload layouts per docs/protocol.md section 8.",
        ],
        "crc_check": {"crc16_ccitt_false_123456789": "29b1", "crc32_iso_hdlc_123456789": "cbf43926"},
        "commands": {k: v for k, v in CMDS.items()},
        "vectors": vectors,
    }


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", default=os.path.join(here, "..", "..", "..", "docs", "protocol-vectors.json"))
    args = ap.parse_args()
    data = build()
    with open(args.output, "w", newline="\n") as fh:
        json.dump(data, fh, indent=1)
        fh.write("\n")
    print(f"wrote {len(data['vectors'])} vectors to {os.path.normpath(args.output)}")


if __name__ == "__main__":
    main()
