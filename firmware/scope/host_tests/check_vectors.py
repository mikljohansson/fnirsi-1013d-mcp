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
    ap.add_argument("--decim-checker", help="decim_check binary (pc_decim.c): cross-check the decim section")
    ap.add_argument("--adc-cal-checker", help="test_adc_cal binary (adc_cal.c): cross-check the adc_cal block")
    ap.add_argument("--seq-checker", help="seq_check binary (pc_seq.c): cross-check the seq section (1.17)")
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

    # 5. decimation maths (1.9): the C implementation against the vectors' Python reference
    if args.decim_checker and "decim" in data:
        dec = data["decim"]
        text = "".join(f"{c['divider']} {c['k_req']} {c['total']} {c['fir_req']}\n" for c in dec["cases"])
        out = subprocess.run([args.decim_checker] + [str(v) for v in dec["levels"]], input=text,
                             capture_output=True, text=True, check=True).stdout.split("\n")
        for c, ln in zip(dec["cases"], out):
            f = [int(x) for x in ln.split()]
            exp = [c["k"], c["n"], c["gain"], c["shift"], c["rate"], c["settle_us"], c["ext_shift"], c["fir"]] + \
                (c["level16"] or [])
            if f != exp:
                fail(f"decim {c['divider']} {c['k_req']} {c['total']} fir {c['fir_req']}: C gave {f}, expected {exp}")
        print(f"decim: {len(dec['cases'])} cases checked against pc_decim.c")

    # 6. calibration data v3 block (1.12): acal_pack of the same content against the Python layout
    if args.adc_cal_checker and "adc_cal_v3" in data:
        out = subprocess.run([args.adc_cal_checker, "--pack"], capture_output=True, text=True, check=True).stdout.strip()
        if out != data["adc_cal_v3"]["block_hex"]:
            fail(f"adc_cal_v3 block: C packs {out}, expected {data['adc_cal_v3']['block_hex']}")
        else:
            print("adc_cal_v3: sector block identical to acal_pack")
    elif args.adc_cal_checker:
        fail("adc_cal_v3 section missing")

    # 7. sequencer maths (1.17): pc_seq.c against the vectors' Python reference
    if args.seq_checker and "seq" in data:
        cases = data["seq"]["cases"]
        text = "".join(c["op"] + " " + " ".join(str(a) for a in c["args"]) + "\n" for c in cases)
        out = subprocess.run([args.seq_checker], input=text, capture_output=True, text=True, check=True).stdout.split("\n")
        for c, ln in zip(cases, out):
            if [int(x) for x in ln.split()] != c["out"]:
                fail(f"seq {c['op']} {c['args']}: C gave {ln}, expected {c['out']}")
        print(f"seq: {len(cases)} cases checked against pc_seq.c")
    elif args.seq_checker:
        fail("seq section missing")

    if fails:
        print(f"{fails} FAILURES")
        return 1
    print("check_vectors: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
