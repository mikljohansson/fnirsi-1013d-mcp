"""Frame codec, CRCs, payload structs and the shared vectors file (docs/protocol-vectors.json)."""

from __future__ import annotations

import binascii
import json
import random
import zlib
from pathlib import Path

import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d import vectors as V

VECTORS_FILE = Path(__file__).resolve().parents[2] / "docs" / "protocol-vectors.json"

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
    (P.InfoResp, 80), (P.StatusResp, 48), (P.SetRemoteReq, 4), (P.BatteryResp, 4), (P.TestPatternReq, 8),
    (P.ChannelConfig, 24), (P.SetChannelReq, 12), (P.Timebase, 16), (P.SetTimebaseReq, 8), (P.Trigger, 28),
    (P.SetTriggerReq, 24), (P.ReadRawReq, 16), (P.ReadRawHeader, 16), (P.ReadTraceReq, 12),
    (P.ReadTraceHeader, 32), (P.TraceChannelMeta, 16), (P.ReadLiveReq, 4), (P.AcquireAvgReq, 20),
    (P.AcquireAvgHeader, 16), (P.Gen, 24), (P.SetGenReq, 20), (P.CalResp, 364), (P.HealthResp, 128), (P.SdIdent, 48), (P.SaveReportResp, 56), (P.ReadScreenReq, 4),
    (P.ReadScreenHeader, 8), (P.SetAcqStateReq, 4), (P.CaptureInfoHeader, 36), (P.CaptureChannelInfo, 16)])
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
    doc = json.loads(VECTORS_FILE.read_text(encoding="utf-8"))
    assert len(doc["vectors"]) >= 20
    assert doc["crc_check"]["crc16_ccitt_false_123456789"] == "29b1"
    assert V.check_file(VECTORS_FILE) == []


def test_host_vectors_present_and_current():
    doc = json.loads(VECTORS_FILE.read_text(encoding="utf-8"))
    names = {v["name"]: v for v in doc["vectors"]}
    for hv in V.build_host_vectors():
        assert hv["name"] in names, f"{hv['name']} missing: run python -m fnirsi1013d.vectors --merge"
        assert names[hv["name"]]["hex"] == hv["hex"]


def test_status_protocol_1_0_body_still_parses():
    st = P.StatusResp(1, 3, 0x0B, 0, 42, pass_ms=7, pass_max_ms=31, settings_seq=9, run_state=1)
    data = st.pack()
    assert len(data) == 48 and P.StatusResp.unpack(data).pass_max_ms == 31
    old = P.StatusResp.unpack(data[:28])  # firmware 0.1.0 / protocol 1.0
    assert old.capture_id == 42 and old.pass_ms == 0 and old.pass_max_ms == 0
    p12 = P.StatusResp.unpack(data[:32])  # protocol 1.1 / 1.2
    assert p12.pass_max_ms == 31 and p12.settings_seq == 0 and not p12.shared
    sh = P.StatusResp.unpack(P.StatusResp(control=2, settings_seq=5, run_state=1).pack())
    assert sh.shared and sh.settings_seq == 5 and sh.run_state == P.RunState.RUN


def test_capture_info_roundtrip_and_views():
    ci = P.CaptureInfo(P.CaptureInfoHeader(capture_id=7, settings_seq=3, origin=1, divider=9, trig_source=1,
                                           pretrigger_samples=800, total_samples=16000),
                       [P.CaptureChannelInfo(range=4, probe_index=2, uv_per_lsb_q8=1000),
                        P.CaptureChannelInfo(range=2, coupling=1)])
    data = ci.pack()
    assert len(data) == 68
    back = P.CaptureInfo.unpack(data)
    assert back == ci
    assert back.trigger().total_words == 8000 and back.timebase().divider == 9
    cfg = back.channel_config(0)
    assert cfg.range == 4 and cfg.hw_range == 4 and cfg.probe_factor == 10.0
    assert back.settings_key()["CH2.coupling"] == 1


def test_capture_info_maybe_spliced_flag():
    """fw 0.2.97: GET_CAPTURE_INFO flags bit 6, the record may be spliced; Capture.maybe_spliced and its caveat."""
    from fnirsi1013d.scope import Capture
    from fnirsi1013d.server import _capture_caveats

    H = P.CaptureInfoHeader
    ci = P.CaptureInfo(H(capture_id=1, flags=H.F_AUTO_TIMEOUT | H.F_MAYBE_SPLICED, total_samples=3000),
                       [P.CaptureChannelInfo(), P.CaptureChannelInfo()])
    back = P.CaptureInfo.unpack(ci.pack())
    assert back.header.flags & H.F_MAYBE_SPLICED
    cap = Capture(1, 1e-8, 0, 0, 0, 3000, False, True, {}, back.trigger(), capture_info=back)
    assert cap.maybe_spliced
    assert any("spliced" in c for c in _capture_caveats(cap, {}))
    clean = P.CaptureInfo(H(capture_id=2, flags=H.F_AUTO_TIMEOUT, total_samples=3000), ci.channels)
    cap = Capture(2, 1e-8, 0, 0, 0, 3000, False, True, {}, clean.trigger(), capture_info=clean)
    assert not cap.maybe_spliced and not any("spliced" in c for c in _capture_caveats(cap, {}))
    assert not Capture(3, 1e-8, 0, 0, 0, 3000, False, True, {}, clean.trigger()).maybe_spliced


def test_link_stats_fw_0_2_4_sample():
    """Lead's raw GET_LINK_STATS on fw 0.2.4: 36 u32 words, word 0 = the count (35), counters follow."""
    import struct as st
    words = [35, 10, 1145, 0, 0, 7, 0, 0, 0, 2, 2, 1, 5, 0, 0, 0, 0, 0, 0, 1026, 4, 0, 0, 0, 1, 6, 0, 14, 1515, 31, 1,
             48, 1, 31, 32, 32]
    d = P.parse_link_stats(st.pack(f"<{len(words)}I", *words))
    assert len(d) == 35 and d["rx_packets"] == 10 and d["rx_bytes"] == 1145 and d["tx_packets"] == 7
    assert d["first_poll_ms"] == 1515 and d["first_pass_ms"] == 31  # counters 27 and 28
    assert d["first_conversion_ms"] == 1 and d["first_after_rtc_ms"] == 32
    assert list(P.parse_link_stats(P.pack_link_stats({}))) == list(P.LINK_STATS_NAMES)  # 31-counter layout
    import struct as st2
    d41 = P.parse_link_stats(st2.pack("<HH41I", 41, 0, *range(41)))
    assert d41["first_after_rtc_ms"] == 34 and d41["usb_bus_resets"] == 35 and d41["ep0_bad_setups"] == 40
    d47 = P.parse_link_stats(st2.pack("<HH47I", 47, 0, *range(47)))  # fw 0.2.7, names as in bench_link.py
    assert d47["rx_isr_stalls"] == 41 and d47["rx_count_oversize"] == 46 and "c46" not in d47


def test_event_log_decode():
    """GET_EVENT_LOG (protocol 1.5, fw 0.2.12): u16 n, u16 0, u32 total, u32 now, n x (u32 ms, u8 type, u8 a, u16 b)."""
    log = P.EventLog(70, 50000, [P.DeviceEvent(49000, 2, 0, 0), P.DeviceEvent(49001, 1, 0, 0),
                                 P.DeviceEvent(49100, 5, 0x22, 0x2101), P.DeviceEvent(49200, 14, 0x04, 0x0102)])
    data = P.pack_event_log(log)
    assert len(data) == 12 + 4 * 8
    back = P.parse_event_log(data)
    assert back == log
    assert [e.name for e in back.events] == ["suspend", "bus_reset", "ep0_setup", "usb_irq_polled"]
    assert back.events[2].describe() == "ep0_setup bRequest 0x22 bmRequestType 0x21 wValue.lo 0x01"
    assert "INTUSB 0x04 INTTX 0x01 INTRX 0x02" in back.events[3].describe()
    assert P.parse_event_log(b"").events == []
    import struct
    d48 = P.parse_link_stats(struct.pack("<HH48I", 48, 0, *range(48)))
    assert d48["usb_irq_polled"] == 47
    d49 = P.parse_link_stats(struct.pack("<HH49I", 49, 0, *range(49)))  # fw 0.2.17
    assert d49["boot_splash_ms"] == 48 and "c48" not in d49


def test_event_log_reset_snapshot_tail():
    """fw 0.2.15: after the n entries, u16 m, u16 frozen, u32 reset_ms, m entries; 0.2.12-0.2.14 end at 12 + 8n."""
    import struct
    old = P.pack_event_log(P.EventLog(3, 900, [P.DeviceEvent(800, 9, 1, 0)]))
    o = P.parse_event_log(old)
    assert not o.has_snapshot and not o.frozen and o.snapshot == []
    snap = [P.DeviceEvent(1000 + i, 5, 0x20, 0x2100) for i in range(3)]
    log = P.EventLog(200, 5000, [P.DeviceEvent(4000 + i, 14, 4, 0) for i in range(128)], frozen=True,
                     reset_ms=1010, snapshot=snap, has_snapshot=True)
    data = P.pack_event_log(log)
    assert len(data) == 12 + 8 * 128 + 8 + 8 * 3
    back = P.parse_event_log(data)
    assert back == log and len(back.events) == 128 and back.frozen and back.reset_ms == 1010
    idle = P.parse_event_log(data[:12 + 8 * 128] + struct.pack("<HHI", 0, 0, 0))
    assert idle.has_snapshot and not idle.frozen



def test_restart_record_fw_0_2_19():
    import struct
    d57 = P.parse_link_stats(struct.pack("<HH57I", 57, 0, *range(57)))
    assert d57["boot_splash_ms"] == 48 and d57["restarts"] == 49 and d57["restart_reset_cpsr"] == 56
    assert P.DeviceEvent(5, 15, 4, 0x1234).describe() == "restart exception 4 (data abort) pc.lo16 0x1234"
