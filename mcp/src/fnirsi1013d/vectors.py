"""Protocol test vectors shared with the firmware: docs/protocol-vectors.json.

The file is owned jointly. The firmware agent generates the base set with
``firmware/scope/host_tests/gen_vectors.py``; this module adds host-side vectors (names prefixed ``host_``,
``"source": "host"``) with ``--merge``, which keeps every existing vector and adds only new names (the union).

Schema (defined by the firmware generator, see the file's ``notes``):

- ``hex``: the byte stream fed to a parser in one go, followed by >= 100 ms of silence;
- ``frames``: structurally valid frames (proto_major 1, both CRCs good) a direction-agnostic parser finds, with
  ``cmd seq status flags len payload``;
- ``device_events``: what the scope's request parser reports, in order: ``frame`` (with the fields above),
  ``bad_crc`` / ``bad_len`` / ``bad_version`` (with ``cmd seq``), ``ascii`` (with ``line`` without the
  ``#``), ``ascii_overflow``. Dropped input (bad header CRC, junk, RESPONSE-flagged frames, partial frames)
  produces no event.

``host_events`` / ``device_events`` below reproduce both views with :class:`fnirsi1013d.protocol.FrameParser`
so the tests can check the Python side against the same file as the C parser.
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path
from typing import Any

from . import protocol as P

DEFAULT_PATH = Path(__file__).resolve().parents[3] / "docs" / "protocol-vectors.json"


def _decoded(f: P.Frame) -> dict[str, Any]:
    return {"cmd": f.cmd, "seq": f.seq, "status": f.status, "flags": f.flags, "len": len(f.payload),
            "payload": f.payload.hex()}


def host_frames(data: bytes) -> list[dict[str, Any]]:
    """Direction-agnostic view: every structurally valid proto-1 frame."""
    p = P.FrameParser(max_payload=P.MAX_RESPONSE_PAYLOAD, ascii_lines=True, max_ascii=P.MAX_REQUEST_FRAME)
    out = [_decoded(e) for e in p.feed(data) if isinstance(e, P.Frame) and e.proto_major == P.PROTO_MAJOR]
    p.reset()  # >= 100 ms of silence after the stream
    return out


def device_events(data: bytes) -> list[dict[str, Any]]:
    """The device's request parser (as in the simulator): 492-byte payload limit, 63-character ASCII lines."""
    p = P.FrameParser(max_payload=P.MAX_REQUEST_PAYLOAD, ascii_lines=True, max_ascii=63)
    out: list[dict[str, Any]] = []
    for e in p.feed(data):
        if isinstance(e, P.Frame):
            if e.is_response:
                continue
            if e.proto_major != P.PROTO_MAJOR:
                out.append({"event": "bad_version", "cmd": e.cmd, "seq": e.seq})
                continue
            out.append({**_decoded(e), "event": "frame"})
        elif isinstance(e, P.BadPayloadCrc):
            if e.frame.is_response:
                continue
            if e.frame.proto_major != P.PROTO_MAJOR:
                out.append({"event": "bad_version", "cmd": e.frame.cmd, "seq": e.frame.seq})
                continue
            out.append({"event": "bad_crc", "cmd": e.frame.cmd, "seq": e.frame.seq})
        elif isinstance(e, P.Oversize):
            if e.frame.is_response:
                continue
            if e.frame.proto_major != P.PROTO_MAJOR:
                out.append({"event": "bad_version", "cmd": e.frame.cmd, "seq": e.frame.seq})
                continue
            out.append({"event": "bad_len", "cmd": e.frame.cmd, "seq": e.frame.seq})
        elif isinstance(e, P.AsciiLine):
            out.append({"event": "ascii", "line": e.text[1:]})
        elif isinstance(e, P.AsciiOverflow):
            out.append({"event": "ascii_overflow"})
    p.reset()
    return out


def _f(cmd: int, seq: int, payload: bytes = b"", status: int = 0, flags: int = 0) -> P.Frame:
    return P.Frame(cmd, seq, payload, status, flags)


def _vec(name: str, data: bytes, description: str) -> dict[str, Any]:
    return {"name": name, "description": description, "hex": data.hex(), "frames": host_frames(data),
            "device_events": device_events(data), "source": "host"}


def build_host_vectors() -> list[dict[str, Any]]:
    """Vectors the host side adds: response payloads the host must decode and a few stream cases."""
    R = P.FLAG_RESPONSE
    v = []
    info = P.InfoResp(fpga_version=0x1532, fpga_variant=2, ring_words=8192, caps=0x1803, gen_clock_hz=50_000_000,
                      max_total_samples=16382, fw_version=b"0.1.0", fw_build=0x1234ABCD, serial=b"A1B2C3D4",
                      proto_minor=0)
    v.append(_vec("host_resp_get_info_al3", _f(P.Cmd.GET_INFO, 1, info.pack(), flags=R).encode(),
                  "GET_INFO response, 76 bytes, AL3 0x1532, caps PROG_TRIGGER_POS|GENERATOR_DDS|READ_LAG_UNVERIFIED|"
                  "INTERLEAVE_UNVERIFIED"))
    st = P.StatusResp(1, 3, 0x0B, 0, 42, 6609, 6209, 3, 123456, 15, 18, 0)
    v.append(_vec("host_resp_get_status_done", _f(P.Cmd.GET_STATUS, 5, st.pack()[:28], flags=R).encode(),
                  "GET_STATUS response (protocol 1.0, 28 bytes): REMOTE, DONE, flags triggered|done|dtr, capture 42"))
    info11 = P.InfoResp(fpga_version=0x1532, fpga_variant=2, ring_words=8192, caps=0x1C03, gen_clock_hz=50_000_000,
                        max_total_samples=16382, fw_version=b"0.1.1", fw_build=0x5A8432BE, serial=b"1013D-0001",
                        proto_minor=1)
    v.append(_vec("host_resp_get_info_al3_proto_1_1", _f(P.Cmd.GET_INFO, 2, info11.pack(), flags=R).encode(),
                  "GET_INFO response of firmware 0.1.1: proto 1.1, caps 0x1C03 (ENTER_FEL added)"))
    st11 = P.StatusResp(1, 3, 0x0B, 0, 43, 6609, 6209, 3, 123456, 15, 18, 0, 0, 7, 31)
    v.append(_vec("host_resp_get_status_proto_1_1", _f(P.Cmd.GET_STATUS, 6, st11.pack(), flags=R).encode(),
                  "GET_STATUS response, protocol 1.1 (32 bytes): pass_ms 7, pass_max_ms 31"))
    v.append(_vec("host_ascii_reset", b"#RESET\n", "#RESET recovery line (firmware 0.1.1)"))
    v.append(_vec("host_resp_e_bad_crc", _f(P.Cmd.PING, 23, P.error_payload(0, "payload crc"), P.Status.E_BAD_CRC,
                                            R).encode(), "E_BAD_CRC answer with detail 0 and text"))
    v.append(_vec("host_resp_e_version", _f(P.Cmd.GET_INFO, 24, b"", P.Status.E_VERSION, R).encode(),
                  "E_VERSION answer without payload"))
    hdr = P.ReadRawHeader(42, 6209, 8192, 4, 0x0F, 0, 3, 0).pack()
    v.append(_vec("host_resp_read_raw_4x4", _f(P.Cmd.READ_RAW, 14, hdr + bytes(range(100, 116)), flags=R).encode(),
                  "READ_RAW response: 4 words x 4 cores, layout 0 (blocks CH1A, CH1B, CH2A, CH2B)"))
    v.append(_vec("host_req_read_raw_trigger_minus_400", _f(P.Cmd.READ_RAW, 15,
                  P.ReadRawReq(0x03, 2, 1, 0, -400, 1000).pack()).encode(),
                  "READ_RAW CH1 both cores, interleaved, 400 words before trigger_addr (i32 -400), 1000 words"))
    v.append(_vec("host_req_set_remote_release_keep", _f(P.Cmd.SET_REMOTE, 7, P.SetRemoteReq(0, 1, 0).pack()).encode(),
                  "release with KEEP_SETTINGS"))
    v.append(_vec("host_req_set_trigger_all_but_holdoff", _f(P.Cmd.SET_TRIGGER, 11,
                  P.SetTriggerReq(0x00BF, 1, 1, 160, 1, 800, 16000, 0, 0).pack()).encode(),
                  "mask 0xBF: CH2 falling level 160 normal, pretrigger 800, total 16000, auto_timeout 0"))
    v.append(_vec("host_req_wait_10000", _f(P.Cmd.WAIT, 13, struct.pack("<I", 10000)).encode(),
                  "WAIT with the maximum timeout 10000 ms"))
    three = _f(P.Cmd.PING, 40, b"a").encode() + b"\xa5\x13\x01\x00" + _f(P.Cmd.PING, 41).encode() + b"\xa5"
    v.append(_vec("host_frames_with_false_magic_and_tail", three,
                  "frame, a false 4-byte header start, frame, lone 0xA5 then silence (partial dropped)"))
    v.append(_vec("host_ascii_63_chars", b"#" + b"Y" * 63 + b"\n",
                  "longest accepted line: 63 characters after '#' (one more is ascii_overflow)"))
    return v


def check_file(path: Path = DEFAULT_PATH) -> list[str]:
    """Return mismatches between the Python parsers and the file."""
    doc = json.loads(path.read_text(encoding="utf-8"))
    errs = []
    for vec in doc["vectors"]:
        data = bytes.fromhex(vec["hex"])
        hf = host_frames(data)
        if hf != vec["frames"]:
            errs.append(f"{vec['name']}: host frames {hf} != {vec['frames']}")
        de = device_events(data)
        exp = [dict(sorted(e.items())) for e in vec["device_events"]]
        if [dict(sorted(e.items())) for e in de] != exp:
            errs.append(f"{vec['name']}: device events {de} != {vec['device_events']}")
    return errs


def main(argv: list[str] | None = None) -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--file", type=Path, default=DEFAULT_PATH)
    ap.add_argument("--merge", action="store_true", help="add host vectors with new names (keeps the rest)")
    ap.add_argument("--check", action="store_true", help="check the Python parsers against the file")
    a = ap.parse_args(argv)
    if a.merge:
        doc = json.loads(a.file.read_text(encoding="utf-8"))
        names = {x["name"] for x in doc["vectors"]}
        new = [x for x in build_host_vectors() if x["name"] not in names]
        doc["vectors"].extend(new)
        with open(a.file, "w", newline="\n", encoding="utf-8") as fh:
            json.dump(doc, fh, indent=1)
            fh.write("\n")
        print(f"added {len(new)} host vectors; {len(doc['vectors'])} total")
    if a.check or not a.merge:
        errs = check_file(a.file)
        for e in errs:
            print("FAIL:", e)
        print("ok" if not errs else f"{len(errs)} mismatches")
        raise SystemExit(1 if errs else 0)


if __name__ == "__main__":
    main()
