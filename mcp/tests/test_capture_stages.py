"""Protocol 1.24 (fw 0.2.79, docs/protocol.md row 64, provisional numbering): FPGA 0x153A capture stages. Caps bit 19
and features bit 7, GET_HEALTH layout 2 (124 bytes, 104..123) against the firmware vector, GET_CAL 364 bytes
(356..363), the sector 708 'ACAP' trailer, event 23, the longer EXT shifts, the simulator's al3c variant and the
tools."""

from __future__ import annotations

import json
import struct
import zlib
from pathlib import Path

import anyio

from fnirsi1013d import protocol as P
from fnirsi1013d.scope import Scope

VECTORS = json.loads((Path(__file__).resolve().parents[2] / "docs" / "protocol-vectors.json").read_text())


def frame_payload(name: str) -> bytes:
    v = next(x for x in VECTORS["vectors"] if x["name"] == name)
    return bytes.fromhex(v["frames"][0]["payload"])


def test_bits_and_version():
    assert P.PROTO_MINOR >= 25
    assert P.Caps.CAPTURE_STAGES == 0x80000 and P.Features.CAPTURE_STAGES == 0x0080
    assert P.feature_names(0xFF)[-1] == "capture_stages"
    assert P.EVENT_TYPES[23] == "adc_capture"


def test_health_1_24_vector():
    raw = frame_payload("resp_get_health_1_24")
    h = P.HealthResp.unpack(raw)
    assert len(raw) == 124 and (h.layout, h.length) == (2, 124) and h.has_capture and h.has_enc_check
    assert (h.cap_value, h.cap_flags, h.cap_flagged, h.cap_counts) == (0x08DC, 0x0F, 0x04, (0, 0, 37, 2))
    assert (h.cap_reads, h.cap_flagged_n) == (1200, 3)
    assert h.pack()[:124] == raw and not h.has_fpga_version and h.describe()["fpga"] is None
    c = h.describe()["capture_stages"]
    assert c["stages"] == "CH1 A4 B3, CH2 A3 B4" and c["source"] == "trained" and c["value"] == "0x08DC"
    assert c["flagged_cores"] == ["CH2 A"] and c["last_counts"]["CH2 A"] == 37
    assert c["text"] == ("capture stages CH1 A4 B3, CH2 A3 B4 (trained); CH2 A: 37 outliers; 3 of 1200 full-rate "
                         "captures flagged since boot")
    assert h.describe()["encode_check"]["text"] == "encode check OK"


def test_health_older_layouts_read_zero():
    for n in (96, 104):
        h = P.HealthResp.unpack(P.HealthResp(layout=1, length=n, cap_value=0x08E4, cap_flags=1).pack()[:n])
        assert not h.has_capture and h.cap_value == 0 and h.cap_counts == (0, 0, 0, 0)
        assert h.describe()["capture_stages"] is None


def test_health_capture_words():
    po = P.HealthResp(cap_value=P.CAP_POWER_ON, cap_flags=P.CAPF_HAS).describe_capture()
    assert po["text"] == "capture stages CH1 A4 B4, CH2 A3 B4 (power-on default); no 200 MSa/s capture read yet"
    assert po["last_counts"] is None
    clean = P.HealthResp(cap_value=P.CAP_POWER_ON, cap_flags=P.CAPF_HAS | P.CAPF_READ | P.CAPF_OTHER,
                         cap_reads=4).describe_capture()
    assert clean["text"].startswith("capture stages CH1 A4 B4, CH2 A3 B4 (power-on default); capture clean; a value "
                                    "stored for another bitstream is not applied")
    assert clean["stored_for_other_bitstream"]
    new = P.HealthResp(cap_value=0x08E3, cap_flags=P.CAPF_HAS).describe_capture()
    assert new["source"] == "not stored (a training not saved yet)"
    assert not new["flagged_repeatedly"]
    # fw 0.2.81: GET_HEALTH 106 bit 5, a core flagged in 3 captures within a minute (the last capture may be clean)
    rep = P.HealthResp(cap_value=0x08E3, cap_flags=P.CAPF_HAS | P.CAPF_TRAINED | P.CAPF_READ | P.CAPF_REPEAT,
                       cap_reads=9).describe_capture()
    assert rep["flagged_repeatedly"]
    assert rep["text"] == ("capture stages CH1 A3 B4, CH2 A3 B4 (trained); capture clean; a core was flagged in 3 "
                           "captures within a minute: run the calibration wizard")
    # fw 0.2.105: GET_HEALTH 106 bit 6, the runtime guard moved a core this boot
    mv = P.HealthResp(cap_value=0x08E3, cap_flags=P.CAPF_HAS | P.CAPF_TRAINED | P.CAPF_READ | P.CAPF_MOVED,
                      cap_reads=9).describe_capture()
    assert mv["moved_by_guard"] and not rep["moved_by_guard"]
    assert mv["text"] == ("capture stages CH1 A3 B4, CH2 A3 B4 (trained); capture clean; a flagged core was moved to "
                          "another trained stage this boot (stored)")
    assert P.HealthResp(cap_flags=0).describe_capture() == {
        "has_stages": False, "text": "no ADC capture stages on this bitstream (FPGA 0x153A and later)"}


def test_health_wire_offsets():
    b = P.HealthResp(cap_value=0x1234, cap_flags=5, cap_flagged=6, cap_counts=(7, 8, 9, 10), cap_reads=11,
                     cap_flagged_n=12).pack()
    assert len(b) == 128 and b[0] == 3 and struct.unpack_from("<H", b, 2) == (128,)  # layout 3 since 1.25
    assert struct.unpack_from("<HBB4HII", b, 104) == (0x1234, 5, 6, 7, 8, 9, 10, 11, 12)


def test_stage_texts_and_self_check():
    assert P.cap_stages_text(P.CAP_POWER_ON) == "CH1 A4 B4, CH2 A3 B4"
    assert P.cap_stages_text(P.CAP_POWER_ON | 0x2000, 1) == "CH2 A3 B4 A-clk"
    assert P.cap_stages_text(P.CAP_POWER_ON | 0x1000) == "CH1 A4 B4 A-clk, CH2 A3 B4"
    assert [P.cap_stage(P.CAP_POWER_ON, k) for k in range(4)] == [4, 4, 3, 4]
    assert P.cap_core_names(0x0A) == ["CH1 B", "CH2 B"]
    assert P.cap_excess((0, 0, 37, 2)) == [0, 0, 35, 0]
    assert P.cap_flagged((0, 0, 37, 2)) == 0x04
    assert P.cap_flagged((9, 0, 8, 0)) == 0x01  # excess 9 > 8 flags, 8 does not
    assert P.cap_flagged((500, 500, 0, 0)) == 0  # a signal moves both cores of a chip alike


def test_cap_trailer():
    b = P.pack_cap_trailer(0x08DC, 0x153A)
    assert len(b) == 16 and b[:4] == b"ACAP" and struct.unpack_from("<HHHH", b, 4) == (1, 16, 0x08DC, 0x153A)
    assert struct.unpack_from("<I", b, 12)[0] == zlib.crc32(b[:12])
    assert P.parse_cap_trailer(b) == (1, 0x08DC, 0x153A)
    assert P.parse_cap_trailer(bytes(16)) == (0, 0, 0)
    assert P.parse_cap_trailer(b[:12] + bytes(4)) == (2, 0, 0)
    assert P.parse_cap_trailer(b"ACAP" + struct.pack("<HH", 2, 16) + b[8:]) == (2, 0, 0)
    sector = bytearray(512)
    sector[P.CAP_TRAILER_OFFSET:P.CAP_TRAILER_OFFSET + 16] = b
    assert P.parse_cap_trailer(bytes(sector[488:504])) == (1, 0x08DC, 0x153A)


def test_get_cal_364():
    r = P.CalResp(cap_trailer=1, cap_flags=P.CAPF_HAS | P.CAPF_TRAINED, cap_stored=0x08DC, cap_stored_fpga=0x153A,
                  cap_value=0x08DC)
    b = r.pack()
    assert len(b) == 364 and struct.unpack_from("<BBHHH", b, 356) == (1, 3, 0x08DC, 0x153A, 0x08DC)
    c = P.CalResp.unpack(b)
    assert c.has_capture and c.has_provenance and c.cap_stored == 0x08DC
    d = c.describe_capture()
    assert d["text"] == ("capture stages CH1 A4 B3, CH2 A3 B4 stored with the calibration (trained on FPGA 0x153A); "
                         "in effect")
    old = P.CalResp.unpack(b[:356])
    assert not old.has_capture and old.describe_capture() is None and old.cap_value == 0
    other = P.CalResp.unpack(P.CalResp(cap_trailer=1, cap_flags=P.CAPF_HAS | P.CAPF_OTHER, cap_stored=0x08DC,
                                       cap_stored_fpga=0x153B, cap_value=P.CAP_POWER_ON).pack())
    assert "not applied on this bitstream, CH1 A4 B4, CH2 A3 B4 in effect" in other.describe_capture()["text"]
    none = P.CalResp.unpack(P.CalResp(cap_flags=P.CAPF_HAS, cap_value=P.CAP_POWER_ON).pack()).describe_capture()
    assert none["text"] == "no capture stages stored with the calibration; CH1 A4 B4, CH2 A3 B4 in effect (untrained)"
    assert "CRC" in P.CalResp.unpack(P.CalResp(cap_trailer=2).pack()).describe_capture()["text"]


def test_event_23_in_words():
    assert P.DeviceEvent(5, 23, 0x04, 35).describe() == \
        "adc_capture self-check flagged CH2 A: 35 outliers beyond the chip partner"
    assert P.DeviceEvent(5, 23, 0x10, 0x08DC).describe() == \
        "adc_capture training ended: now CH1 A4 B3, CH2 A3 B4 (0x08DC)"
    assert P.DeviceEvent(5, 23, 0x12, P.CAP_POWER_ON).describe() == \
        "adc_capture training ended: no clean stage for CH1 B, kept CH1 A4 B4, CH2 A3 B4 (0x08E4)"
    # fw 0.2.105: the runtime guard moved CH1 A off its flagged stage 4 (a bit 5)
    assert P.DeviceEvent(5, 23, 0x21, 0x08E3).describe() == \
        "adc_capture guard moved CH1 A: now CH1 A3 B4, CH2 A3 B4 (0x08E3)"


def test_cal_probe_zero_split_zero_since_0_2_79():
    r = P.CalProbeResp(0, 0, 0, 0xFF, 0, 0, 0, 0, 0, 900, 905, 0, 0,
                       [P.CalProbePoint(i, 0, 0, 0, (P.CalProbeCore(0, 0, 0, 0, 0, 0),) * 2) for i in range(8)])
    d = P.CalProbeResp.unpack(r.pack()).describe()
    assert d["zero_split_pwm"] is None and d["zero_pwm"] == 905


def test_ext_shifts_0x153a():
    cap = P.DECIM_HAS_FIR | P.DECIM_HAS_CAPSTAGES
    assert P.ext_shift_legacy(0, True) == 12 and P.ext_shift_legacy(9, True) == 14
    assert P.ext_shift_legacy(0) == 4 and P.ext_shift_legacy(9) == 8
    leg = P.decim_config(0, 0, 9, 4000, cap)
    assert leg.cap and leg.ext_shift_at(9) == 14 and leg.ext_shift_at(0) == 12
    # K >= 1: 40 ns more lag; CIC2 at divider 9 (N 10): (120 + 2 x 9 x 2.5 + 40) / 50 ns = 4.1 -> 4 (3 without)
    a = P.decim_config(2, 0, 9, 4000, P.DECIM_HAS_CIC)
    b = P.decim_config(2, 0, 9, 4000, P.DECIM_HAS_CIC | P.DECIM_HAS_CAPSTAGES)
    assert (a.ext_shift, b.ext_shift) == (3, 4) and (a.n, a.gain, a.shift) == (b.n, b.gain, b.shift)
    # with the FIR, N 40 (divider 39): lag 250 + 700 + 1.25 x 2 x 38 ns = 1045 ns (+40), period 200 ns
    f = P.decim_config(2, P.DECIM_SET_FIR, 39, 4000, P.DECIM_HAS_FIR)
    g = P.decim_config(2, P.DECIM_SET_FIR, 39, 4000, cap)
    assert f.fir and g.fir and (f.ext_shift, g.ext_shift) == (5, 5)
    h = P.decim_config(2, P.DECIM_SET_FIR, 19, 4000, cap)  # N 20: (250 + 350 + 45 + 40) / 100 = 6.85 -> 7
    assert h.ext_shift == 7 and P.decim_config(2, P.DECIM_SET_FIR, 19, 4000, P.DECIM_HAS_FIR).ext_shift == 6
    # 0x53 carries K alone (bits 4..2 gone in 0x153C, fw-cruft): other request bits do not reach the mode
    assert P.decim_config(2, 0x1C, 9, 4000, cap).mode == 2 and P.decim_config(2, 0x1C, 9, 4000, cap).status == 0x82
    assert P.decim_config(0, 0, 9, 4000, P.DECIM_HAS_NONE | P.DECIM_HAS_CAPSTAGES).why == 6


def test_simulator_al3c(sim_factory):
    srv = sim_factory(signal="sine:freq=10kHz,amp=0.25")
    d = srv.device
    with Scope.connect(srv.url) as s:
        assert s.info.caps & P.Caps.CAPTURE_STAGES and s.info.proto_minor >= 25
        assert s.status().features & P.Features.CAPTURE_STAGES
        h = s.health()
        assert (h.layout, h.length, h.cap_value, h.cap_flags, h.cap_reads) == (3, 128, P.CAP_POWER_ON, P.CAPF_HAS, 0)
        c = s.get_cal(0, cached=False)
        assert c.nbytes == 364 and (c.cap_trailer, c.cap_flags, c.cap_value) == (0, P.CAPF_HAS, P.CAP_POWER_ON)
        s.set_timebase(divider=0)
        s.capture(["CH1"])
        h = s.health()
        assert h.cap_reads == 1 and h.cap_flags == P.CAPF_HAS | P.CAPF_READ and h.cap_flagged == 0
        assert h.describe_capture()["text"].endswith("capture clean")
        d.cap_counts_next = [(0, 0, 40, 1), (0, 0, 41, 1), (0, 0, 0, 0)]
        s.capture(["CH1"])
        s.capture(["CH1"])
        h = s.health()
        assert h.cap_flags & P.CAPF_FLAGGED and h.cap_flagged == 0x04 and h.cap_counts == (0, 0, 41, 1)
        assert (h.cap_reads, h.cap_flagged_n) == (3, 2)
        s.capture(["CH1"])
        assert not s.health().cap_flags & P.CAPF_FLAGGED
        ev = [e for e in s.event_log().events if e.type == 23]
        assert [(e.a, e.b) for e in ev] == [(0x04, 39)]  # once per change of the flagged set
        s.set_timebase(divider=9)  # not full rate: not read
        s.capture(["CH1"])
        assert s.health().cap_reads == 4
        assert s.get_decim().ext_shift == 0  # CH1 trigger
        d.cap_stored = (0x08DC, P.FW_FPGA_VERSION)  # trained on the bitstream the scope runs
        d.cap_boot()
        c = s.get_cal(1, cached=False)
        assert (c.cap_trailer, c.cap_stored, c.cap_stored_fpga, c.cap_value) == \
            (1, 0x08DC, P.FW_FPGA_VERSION, 0x08DC)
        assert c.cap_flags == P.CAPF_HAS | P.CAPF_TRAINED
        d.cap_stored = (0x08DC, 0x153A)  # trained on another bitstream: not applied
        d.cap_boot()
        assert s.health().cap_flags & (P.CAPF_OTHER | P.CAPF_TRAINED) == P.CAPF_OTHER


def test_simulator_al3c_ext_shift(sim_factory):
    srv = sim_factory(signal="sine:freq=10kHz,amp=0.25")
    d = srv.device
    s = d.settings
    dec = d.decim_config(s)
    assert dec.cap and dec.ext_shift_at(0) == 12 and dec.ext_shift_at(9) == 14
    assert d.ext_shift_for(dec, 9, 2, 1000) == 14 and d.ext_shift_for(dec, 0, 2, 1000) == 12


def test_simulator_older_protocol_lengths(sim_factory):
    srv = sim_factory(proto_minor=23)
    with Scope.connect(srv.url) as s:
        assert not s.info.caps & P.Caps.CAPTURE_STAGES
        assert not s.status().features & P.Features.CAPTURE_STAGES
        h = s.health()
        assert (h.layout, h.length) == (1, 104) and not h.has_capture
        assert s.get_cal(0, cached=False).nbytes == 356


def test_tools(sim_factory, tmp_path):
    from test_server import client_for

    srv = sim_factory(signal="sine:freq=10kHz,amp=0.25")
    d = srv.device

    async def main():
        async with client_for(srv, tmp_path) as c:
            await c.call("scope_set_timebase", sample_rate="200MS/s")
            await c.call("scope_capture", channels=["CH1"])
            h = await c.call("scope_health")
            assert h["capture_stages"]["stages"] == "CH1 A4 B4, CH2 A3 B4"
            assert "ADC capture stages CH1 A4 B4, CH2 A3 B4 (power-on default); capture clean" in h["in_words"]
            st = await c.call("scope_status")
            assert "capture_stages" in st["features"]
            ci = await c.call("scope_calibration_info")
            assert ci["capture_stages"]["text"].startswith("no capture stages stored with the calibration")
            d.cap_counts_next = [(0, 30, 0, 0)]
            await c.call("scope_capture", channels=["CH1"])
            r = await c.call("scope_capture", channels=["CH1"])  # the caveat is about the scope's state before it
            assert any("flagged by the ADC capture self-check (CH1 B 30 outliers)" in x for x in r["caveats"])
            r = await c.call("scope_capture", channels=["CH1"])
            assert not any("capture self-check" in x for x in r["caveats"])
    anyio.run(main)
