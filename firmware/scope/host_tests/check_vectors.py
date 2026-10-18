#!/usr/bin/env python3
"""Cross-check the firmware frame parser (pc_protocol.c, via ./vector_check) against Python's CRCs.

1. Every vector in docs/protocol-vectors.json is re-verified with binascii.crc_hqx / zlib.crc32 only:
   each listed frame is re-encoded from its decoded fields and must occur in the vector bytes.
2. The C parser must report exactly the vector's device_events.
3. Corruption: every valid request vector is fed again with a single bit flipped in each byte and
   truncated at every length; the C parser must never report a frame, and must report bad_crc
   for flips inside the payload or payload CRC.
4. Stream: all request vectors concatenated with random junk in between (seeded) must give the
   concatenated events.

Usage: python3 check_vectors.py --checker ./vector_check [--vectors ../../../docs/protocol-vectors.json]
Exit status 0 when everything passes.
"""

import argparse
import binascii
import json
import os
import random
import struct
import subprocess
import sys
import zlib


def encode(d):
    payload = bytes.fromhex(d["payload"])
    h = b"\xa5\x13" + struct.pack("<BBHHHI", 1, d["flags"], d["cmd"], d["seq"], d["status"], len(payload))
    h += struct.pack("<H", binascii.crc_hqx(h, 0xFFFF))
    if payload:
        h += payload + struct.pack("<I", zlib.crc32(payload) & 0xFFFFFFFF)
    return h


def run_checker(checker, streams):
    """streams: list of (name, bytes). Returns {name: [event dicts]}."""
    text = "".join(f"{n} {(b.hex() or '-')}\n" for n, b in streams)
    out = subprocess.run([checker], input=text, capture_output=True, text=True, check=True).stdout
    res = {n: [] for n, _ in streams}
    for ln in out.splitlines():
        f = ln.split()
        if f[0] != "EV":
            continue
        name, kind = f[1], f[2]
        if kind == "frame":
            res[name].append({"event": "frame", "cmd": int(f[3]), "seq": int(f[4]), "status": int(f[5]),
                              "flags": int(f[6]), "len": int(f[7]), "payload": "" if f[8] == "-" else f[8]})
        elif kind in ("bad_crc", "bad_len", "bad_version"):
            res[name].append({"event": kind, "cmd": int(f[3]), "seq": int(f[4])})
        elif kind == "ascii":
            res[name].append({"event": "ascii", "line": "" if f[3] == "-" else bytes.fromhex(f[3]).decode("latin-1")})
        else:
            res[name].append({"event": kind})
    return res


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("--checker", default=os.path.join(here, "vector_check"))
    ap.add_argument("--vectors", default=os.path.join(here, "..", "..", "..", "docs", "protocol-vectors.json"))
    args = ap.parse_args()

    with open(args.vectors) as fh:
        data = json.load(fh)
    vectors = data["vectors"]
    fails = 0

    def fail(msg):
        nonlocal fails
        fails += 1
        print("FAIL:", msg)

    # CRC definitions
    if "%04x" % binascii.crc_hqx(b"123456789", 0xFFFF) != data["crc_check"]["crc16_ccitt_false_123456789"]:
        fail("crc16 check value")
    if "%08x" % (zlib.crc32(b"123456789") & 0xFFFFFFFF) != data["crc_check"]["crc32_iso_hdlc_123456789"]:
        fail("crc32 check value")

    # 1. Python re-verification
    for v in vectors:
        raw = bytes.fromhex(v["hex"])
        for d in v["frames"]:
            if encode(d) not in raw:
                fail(f"{v['name']}: frame {d} not found with Python CRCs")
    names = [v["name"] for v in vectors]
    if len(set(names)) != len(names):
        fail("duplicate vector names")

    # 2. C parser events
    got = run_checker(args.checker, [(v["name"], bytes.fromhex(v["hex"])) for v in vectors])
    for v in vectors:
        exp = [dict(e) for e in v["device_events"]]
        for e in exp:
            if e["event"] == "frame":
                e.pop("len", None)
        g = [dict(e) for e in got[v["name"]]]
        for e in g:
            if e["event"] == "frame":
                e.pop("len", None)
        if g != exp:
            fail(f"{v['name']}: C parser gave {g}, expected {exp}")
    print(f"vectors: {len(vectors)} checked against the C parser")

    # 3. corruption of every pure single-frame request vector
    rnd = random.Random(1013)
    streams, expect_badcrc = [], {}
    clean = [v for v in vectors
             if len(v["device_events"]) == 1 and v["device_events"][0]["event"] == "frame"
             and len(v["frames"]) == 1 and encode(v["frames"][0]) == bytes.fromhex(v["hex"])]
    for v in clean:
        raw = bytes.fromhex(v["hex"])
        for i in range(len(raw)):
            bad = bytearray(raw)
            bad[i] ^= 1 << rnd.randrange(8)
            n = f"{v['name']}#flip{i}"
            streams.append((n, bytes(bad)))
            expect_badcrc[n] = i >= 16
        for k in range(len(raw)):
            streams.append((f"{v['name']}#trunc{k}", raw[:k]))
    res = run_checker(args.checker, streams)
    for n, evs in res.items():
        if any(e["event"] == "frame" for e in evs):
            fail(f"{n}: corrupted input accepted as a frame: {evs}")
        if n in expect_badcrc:
            nbad = sum(1 for e in evs if e["event"] == "bad_crc")
            if expect_badcrc[n] and nbad != 1:
                fail(f"{n}: expected one bad_crc, got {evs}")
            if not expect_badcrc[n] and nbad:
                fail(f"{n}: header corruption gave bad_crc: {evs}")
    print(f"corruption: {len(streams)} corrupted / truncated streams from {len(clean)} frames, none accepted")

    # 4. one long stream with junk in between
    stream, exp = b"", []
    for v in clean:
        junk = bytes(rnd.choice(b"ATZ\r\n\x00\xff\x13") for _ in range(rnd.randrange(0, 6)))
        stream += junk + bytes.fromhex(v["hex"])
        exp += v["device_events"]
    g = run_checker(args.checker, [("stream", stream)])["stream"]
    strip = lambda evs: [{k: x for k, x in e.items() if k != "len"} for e in evs]
    if strip(g) != strip(exp):
        fail(f"stream: {len(g)} events, expected {len(exp)}")
    else:
        print(f"stream: {len(exp)} frames in one {len(stream)}-byte stream with junk, all found")

    if fails:
        print(f"{fails} FAILURES")
        return 1
    print("check_vectors: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
