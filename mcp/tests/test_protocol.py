"""Frame codec, CRCs, payload structs and the shared vectors file (docs/protocol-vectors.json)."""

from __future__ import annotations

import binascii
import json
import random
import zlib

import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d import vectors as V

SPEC_FRAMES = [  # docs/protocol.md section 13
    ("a5 13 01 00 02 00 01 00 00 00 00 00 00 00 22 a6", P.Frame(P.Cmd.GET_INFO, 1)),
    ("a5 13 01 00 01 00 02 00 00 00 02 00 00 00 f0 32 68 69 ac 2a 93 d8", P.Frame(P.Cmd.PING, 2, b"hi")),
    ("a5 13 01 01 01 00 02 00 00 00 02 00 00 00 b9 ea 68 69 ac 2a 93 d8", P.Frame(P.Cmd.PING, 2, b"hi", flags=1)),
    ("a5 13 01 00 03 01 03 00 00 00 08 00 00 00 21 28 00 00 00 00 13 00 00 00 18 27 8e 27",
     P.Frame(P.Cmd.SET_TIMEBASE, 3, P.SetTimebaseReq(0, 19).pack())),
    ("a5 13 01 01 01 01 04 00 06 00 00 00 00 00 d8 e1", P.Frame(P.Cmd.SET_CHANNEL, 4, b"", P.Status.E_NOT_REMOTE, 1)),
]


def test_crc_check_values():
    assert P.crc16_ccitt_false(b"123456789") == 0x29B1
    assert P.crc32_iso_hdlc(b"123456789") == 0xCBF43926
    assert binascii.crc_hqx(b"123456789", 0xFFFF) == 0x29B1 and zlib.crc32(b"123456789") == 0xCBF43926


@pytest.mark.parametrize("hexstr,frame", SPEC_FRAMES)
def test_spec_example_frames(hexstr, frame):
    data = bytes.fromhex(hexstr)
    assert frame.encode() == data
    dec = P.decode_frame(data)
    assert (dec.cmd, dec.seq, dec.payload, dec.status, dec.flags) == (frame.cmd, frame.seq, frame.payload,
                                                                     frame.status, frame.flags)


@pytest.mark.parametrize("cls,size", [
    (P.InfoResp, 76), (P.StatusResp, 32), (P.SetRemoteReq, 4), (P.BatteryResp, 4), (P.TestPatternReq, 8),
    (P.ChannelConfig, 24), (P.SetChannelReq, 12), (P.Timebase, 16), (P.SetTimebaseReq, 8), (P.Trigger, 28),
    (P.SetTriggerReq, 24), (P.ReadRawReq, 16), (P.ReadRawHeader, 16), (P.ReadTraceReq, 12),
    (P.ReadTraceHeader, 32), (P.TraceChannelMeta, 16), (P.ReadLiveReq, 4), (P.AcquireAvgReq, 20),
    (P.AcquireAvgHeader, 16), (P.Gen, 24), (P.SetGenReq, 20), (P.CalResp, 72), (P.ReadScreenReq, 4),
    (P.ReadScreenHeader, 8)])
def test_struct_sizes_match_spec(cls, size):
    assert cls.size() == size


def test_payload_compat_longer_and_shorter():
    st = P.StatusResp(1, 3, 0x0B, 0, 42, 6609, 6209, 3, 123456, 15, 18, 2)
    data = st.pack()
    assert P.StatusResp.unpack(data + b"\x01\x02\x03") == st  # tail ignored
    short = P.StatusResp.unpack(data[:8])  # missing trailing fields are zero
    assert short.capture_id == 42 and short.trigger_addr == 0 and short.exit_reason == 0


def test_info_roundtrip_strings():
    i = P.InfoResp(fpga_version=0x1532, fw_version=b"0.1.0", serial=b"ABCDEF01")
    j = P.InfoResp.unpack(i.pack())
    assert j.fw_version_str == "0.1.0" and j.serial_str == "ABCDEF01" and j.fpga_version == 0x1532


def test_cal_arrays_roundtrip():
    c = P.CalResp(1, tuple(range(1000, 1007)), P.DEFAULT_INPUT_CALIBRATION, -3, 2, 1, 2, 3, 4, 5)
    assert P.CalResp.unpack(c.pack()) == c


def test_parser_any_chunking():
    frames = [P.Frame(P.Cmd.PING, i, bytes(range(i % 50))) for i in range(1, 40)]
    stream = b"".join(f.encode() for f in frames)
    rng = random.Random(5)
    p = P.FrameParser()
    got = []
    i = 0
    while i < len(stream):
        n = rng.randint(1, 37)
        got += [e for e in p.feed(stream[i:i + n]) if isinstance(e, P.Frame)]
        i += n
    assert [(g.seq, g.payload) for g in got] == [(f.seq, f.payload) for f in frames]


def test_parser_resync_after_garbage_and_bad_header():
    good = P.Frame(P.Cmd.GET_STATUS, 7).encode()
    bad = bytearray(P.Frame(P.Cmd.PING, 8, b"xx").encode())
    bad[5] ^= 0x40
    p = P.FrameParser()
    ev = p.feed(b"AT\r\n\x00\xa5\xa5" + bytes(bad) + b"\xa5\x13" + good)
    frames = [e for e in ev if isinstance(e, P.Frame)]
    assert [f.seq for f in frames] == [7]
    assert any(isinstance(e, P.BadHeader) for e in ev)


def test_parser_oversize_is_skipped():
    hdr = P.Frame(P.Cmd.PING, 9, b"\0" * 600).encode()
    p = P.FrameParser(max_payload=P.MAX_REQUEST_PAYLOAD)
    ev = p.feed(hdr + P.Frame(P.Cmd.PING, 10).encode())
    assert isinstance(ev[0], P.Oversize) and ev[0].length == 600
    assert isinstance(ev[1], P.Frame) and ev[1].seq == 10


def test_decode_frame_errors():
    good = P.Frame(P.Cmd.PING, 1, b"abc").encode()
    with pytest.raises(P.ProtocolError, match="bad_magic"):
        P.decode_frame(b"\x5a" + good[1:])
    with pytest.raises(P.ProtocolError, match="truncated"):
        P.decode_frame(good[:-2])
    bad = bytearray(good)
    bad[17] ^= 1
    with pytest.raises(P.ProtocolError, match="bad_crc"):
        P.decode_frame(bytes(bad))


def test_debug_ops_roundtrip():
    ops = [(P.DBG_WRITE_CMD, 0x1F), (P.DBG_WRITE_DATA, b"\x01\x02"), (P.DBG_READ_DATA, 256), (P.DBG_READ_DATA, 3)]
    assert P.decode_debug_ops(P.encode_debug_ops(ops)) == ops


def test_generator_conversions():
    inc = P.gen_phase_inc(10_000_000)  # 10 kHz
    assert abs(P.gen_freq_mhz(inc) - 10_000_000) <= 12  # 11.64 mHz steps
    assert P.gen_duty_bp(P.gen_threshold(3000)) == 3000


def test_scaling_example_from_spec():
    # range 5 (100 mV/div), input_calibration 3,600,000, mul 200 -> 3.43 mV per code
    assert abs(P.uv_per_lsb_q8(3_600_000, 5) / 256 - 3433.2) < 0.5


def test_shared_vectors_file():
    doc = json.loads(V.DEFAULT_PATH.read_text(encoding="utf-8"))
    assert len(doc["vectors"]) >= 20
    assert doc["crc_check"]["crc16_ccitt_false_123456789"] == "29b1"
    assert V.check_file() == []


def test_host_vectors_present_and_current():
    doc = json.loads(V.DEFAULT_PATH.read_text(encoding="utf-8"))
    names = {v["name"]: v for v in doc["vectors"]}
    for hv in V.build_host_vectors():
        assert hv["name"] in names, f"{hv['name']} missing: run python -m fnirsi1013d.vectors --merge"
        assert names[hv["name"]]["hex"] == hv["hex"]


def test_status_protocol_1_0_body_still_parses():
    st = P.StatusResp(1, 3, 0x0B, 0, 42, pass_ms=7, pass_max_ms=31)
    data = st.pack()
    assert len(data) == 32 and P.StatusResp.unpack(data).pass_max_ms == 31
    old = P.StatusResp.unpack(data[:28])  # firmware 0.1.0 / protocol 1.0
    assert old.capture_id == 42 and old.pass_ms == 0 and old.pass_max_ms == 0
