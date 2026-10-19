"""Protocol 1.19 (fw 0.2.62, docs/protocol.md row 58; 1.18 row 57): every full-rate capture checked for a garbled ADC
core. Caps ENC_CHECK and features bit 5 (FPGA 0x1538), GET_HEALTH 104 bytes (88..103) against the firmware vector,
event types 21 / 22 in words, the simulator's counters and hooks, scope_health and the capture caveat."""

from __future__ import annotations

import json
import struct
from pathlib import Path

import anyio

from fnirsi1013d import protocol as P
from fnirsi1013d.scope import Scope

VECTORS = json.loads((Path(__file__).resolve().parents[2] / "docs" / "protocol-vectors.json").read_text())


def frame_payload(name: str) -> bytes:
    v = next(x for x in VECTORS["vectors"] if x["name"] == name)
    return bytes.fromhex(v["frames"][0]["payload"])


def test_health_1_19_vector():
    raw = frame_payload("resp_get_health_1_19")
    h = P.HealthResp.unpack(raw)
    assert len(raw) == 104 and h.length == 104 and h.has_enc_check
    assert (h.enc_mode, h.enc_last33, h.enc_flags, h.enc_boot33) == (2, 0x77, 0x05, 0xF6)
    assert (h.enc_bad, h.enc_recovered, h.enc_checked) == (3, 2, 1200)
    assert h.pack() == raw
    e = h.describe()["encode_check"]
    assert e["mode"] == "FPGA read 0x33" and e["last_33"] == "0x77" and e["boot_33"] == "0xF6"
    assert e["last_33_bad_pins"] == ["CH2 A"] and e["last_capture_accepted_bad"]
    assert e["captures_accepted_bad_or_abandoned"] == 1
    assert "accepted garbled after 16 re-arms (CH2 A)" in e["text"]


def test_health_96_bytes_has_no_encode_check():
    old = P.HealthResp(length=96).pack()[:96]
    h = P.HealthResp.unpack(old)
    assert not h.has_enc_check and h.describe()["encode_check"] is None


def test_caps_features_and_pins():
    assert P.Caps.ENC_CHECK == 0x40000 and P.Features.ENC_CHECK == 0x20
    assert P.feature_names(0x3F)[-1] == "enc_check"
    assert P.enc33_bad_pins(P.ENC33_GOOD) == [] and P.enc33_bad_pins(0x07) == ["CH2 A", "CH2 B", "CH1 A", "CH1 B"]
    ok = P.HealthResp(enc_mode=2, enc_last33=0xF6, enc_flags=P.ENC_READ, enc_boot33=0xF6, enc_checked=5, enc_bad=2,
                      enc_recovered=2).describe_enc()
    assert ok["text"] == "encode check OK, 2 captures re-taken"
    assert P.HealthResp(enc_mode=2, enc_boot33=0xF2, enc_flags=P.ENC_CONVENTION_BAD).describe_enc()["text"] == \
        "unknown encode convention, 0x33 = 0xF2 at boot"
    assert P.HealthResp(enc_mode=1).describe_enc()["text"] == "encode check, no 200 MSa/s capture yet"
    assert P.HealthResp(enc_mode=0).describe_enc()["mode"] == "none"
    assert "recalibrate" in P.HealthResp(enc_mode=1, enc_checked=1, enc_flags=P.ENC_ORDER_DIFFERS).describe_enc()["text"]


def test_event_types_21_and_22_in_words():
    ev = P.DeviceEvent(10, 21, (2 << 5) | 3, (1 << 12) | 0x77)
    assert ev.describe() == "adc_fullrate_garbled (FPGA 0x33, host): 0x33 = 0x77 (CH2 A wrong), 3 re-arm(s), recovered"
    ev = P.DeviceEvent(10, 21, 0x80 | (1 << 5) | 16, (0 << 12) | 320)
    assert ev.describe() == ("adc_fullrate_garbled (record data check, UI): residual 20.00 codes, 16 re-arm(s), NOT "
                             "recovered (accepted garbled)")
    assert "boot convention check: 0x33 = 0xF2" in P.DeviceEvent(0, 21, 0xE0, 0xF2).describe()
    assert P.DeviceEvent(0, 22, 0x0D, (4 << 12) | 1).describe() == "fpga_write 0x0D = 1 by check"
    assert P.DeviceEvent(0, 22, 0x01, (2 << 12) | 0xFFF).describe() == "fpga_write 0x01 = 4095+ by calibration wizard"
    names = P.link_stats_names(64)
    assert names[62:64] == ("adc_fullrate_bad", "adc_fullrate_recovered") or \
        list(names).index("adc_fullrate_bad") + 1 == list(names).index("adc_fullrate_recovered")
    # 1.22 (fw 0.2.70): 68 counters, 64..67 the FPGA writes and the captures judged by their data
    names = P.link_stats_names(68)
    assert len(names) == 68 and names[64:68] == ("fpga_rate_writes", "fpga_filter_writes", "fpga_sampling_resets",
                                                  "adc_fullrate_by_data")


def test_event_log_fpga_trace_switch(sim_factory):
    """1.22: GET_EVENT_LOG flags bits 2:1 switch event type 22 on / off; the response word at 2 says the level."""
    srv = sim_factory(variant="al3e")
    with Scope.connect(srv.url) as s:
        assert s.event_log().fpga_trace is False
        assert s.event_log(fpga_trace=True).fpga_trace is True and srv.device.fpga_trace
        assert s.event_log().fpga_trace is True
        assert s.event_log(fpga_trace=False).fpga_trace is False
    log = P.EventLog(5, 100, [], fpga_trace=True)
    assert P.parse_event_log(P.pack_event_log(log)).fpga_trace is True
    stats = P.parse_link_stats(frame_payload("resp_get_link_stats_1_22"))
    assert (stats["fpga_rate_writes"], stats["fpga_sampling_resets"], stats["adc_fullrate_by_data"]) == (41, 5803, 112)


def test_event_log_tool_fpga_trace(sim_factory, tmp_path):
    from test_server import client_for

    srv = sim_factory(variant="al3e")

    async def main():
        async with client_for(srv, tmp_path) as c:
            r = await c.call("scope_event_log")
            assert r["fpga_write_trace"] is False
            r = await c.call("scope_event_log", fpga_trace=True)
            assert r["fpga_write_trace"] is True and srv.device.fpga_trace
            r = await c.call("scope_event_log", fpga_trace=False)
            assert r["fpga_write_trace"] is False
    anyio.run(main)


def test_simulator_counts_full_rate_captures(sim_factory):
    srv = sim_factory(variant="al3e", signal="sine:freq=10kHz,amp=0.25")
    d = srv.device
    with Scope.connect(srv.url) as s:
        assert s.info.caps & P.Caps.ENC_CHECK and s.info.proto_minor >= 19
        assert s.status().features & P.Features.ENC_CHECK
        h = s.health()
        assert h.length == 104 and (h.enc_mode, h.enc_checked, h.enc_boot33) == (2, 0, 0xF6)
        assert h.describe_enc()["text"] == "encode check, no 200 MSa/s capture yet"
        s.set_timebase(divider=0)
        assert s.capture(["CH1"]) is not None
        d.enc_bad_next, d.enc_rearms = 1, 2
        assert s.capture(["CH1"]) is not None
        h = s.health()
        assert (h.enc_checked, h.enc_bad, h.enc_recovered, h.enc_last33) == (2, 1, 1, 0xF6)
        assert h.describe_enc()["text"] == "encode check OK, 1 captures re-taken"
        d.enc_unrecovered_next = 1
        s.capture(["CH1"])
        h = s.health()
        assert h.enc_flags & P.ENC_LAST_BAD and h.enc_last33 == 0x77
        ev = [e for e in s.event_log().events if e.type == 21]
        assert [e.a for e in ev] == [(2 << 5) | 2, 0x80 | (2 << 5) | 16] and all(e.b >> 12 == 1 for e in ev)
        s.set_timebase(divider=9)  # not full rate: not checked
        s.capture(["CH1"])
        assert s.health().enc_checked == 3
    srv = sim_factory(variant="al3f")
    with Scope.connect(srv.url) as s:
        h = s.health()
        assert h.enc_mode == 1 and not s.info.caps & P.Caps.ENC_CHECK
        assert not s.status().features & P.Features.ENC_CHECK
    with Scope.connect(sim_factory(variant="stock").url) as s:
        assert s.health().enc_mode == 0


def test_health_tool_and_capture_caveat(sim_factory, tmp_path):
    from test_server import client_for

    srv = sim_factory(variant="al3e", signal="sine:freq=10kHz,amp=0.25")
    d = srv.device

    async def main():
        async with client_for(srv, tmp_path) as c:
            await c.call("scope_set_timebase", sample_rate="200MS/s")
            await c.call("scope_capture", channels=["CH1"])
            h = await c.call("scope_health")
            assert h["encode_check"]["captures_checked"] == 1
            assert any(w.startswith("full-rate capture check (FPGA read 0x33): encode check OK") for w in h["in_words"])
            st = await c.call("scope_status")
            assert "enc_check" in st["features"]
            d.enc_unrecovered_next = 1
            await c.call("scope_capture", channels=["CH1"])
            r = await c.call("scope_capture", channels=["CH1"])  # the caveat is about the scope's state before it
            assert any("accepted garbled after 16 re-arms (encode check: CH2 A)" in x for x in r["caveats"])
            r = await c.call("scope_capture", channels=["CH1"])
            assert not any("accepted garbled" in x for x in r["caveats"])
    anyio.run(main)


def test_cal_probe(sim_factory, tmp_path):
    """Protocol 1.21 (row 61, provisional numbering): CAL_PROBE request vector, the 252-byte answer, the simulator's
    wizard model (OK on a quiet input, NOISE on a signal, the E_STATE / E_BAD_ARG rules) and scope_cal_probe."""
    from fnirsi1013d.transport import DeviceError
    import pytest
    from test_server import client_for

    assert P.Cmd.CAL_PROBE == 0x0402 and P.CalProbeReq(1, 0).pack() == frame_payload("req_cal_probe")
    srv = sim_factory(signal="zero")
    with Scope.connect(srv.url) as s:
        assert s.info.proto_minor >= 21
        with pytest.raises(DeviceError) as e:
            s.cal_probe(0, 1)
        assert (e.value.status, e.value.detail) == (P.Status.E_STATE, 1)  # the UI runs
        s.set_acq_state(False)
        for req, det in (((2, 0), 0), ((0, 6), 1)):
            with pytest.raises(DeviceError) as e:
                s.cal_probe(*req)
            assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, det)
        r = s.cal_probe(1, 2, settle=3, prestrobe=1)
        assert (r.channel, r.range, r.result, r.fail_point, r.settle_reads) == (1, 2, "OK", 0xFF, 3)
        assert r.flags & P.CALP_PRESTROBE and 8 <= r.split_d <= 120 and 0 < r.zero_pwm < 2000
        raw = r.pack()
        assert len(raw) == P.CAL_PROBE_BYTES and P.CalProbeResp.unpack(raw).pack() == raw
        pts = [p for p in r.points if p.measured]
        assert [p.conversions for p in pts] == [2, 2, 4, 4, 16, 128, 64, 64]
        assert abs(pts[5].cores[0].mean - 128) < 2 and pts[5].cores[1].mean - pts[5].cores[0].mean == \
            pytest.approx(-2.0, abs=0.3)  # CH2's core B offset in the model
        d = r.describe()
        assert d["channel"] == "CH2" and d["volts_per_div"] == 1.0 and d["failure"] is None
        assert d["points"][5]["point"] == "full-rate zero (pair)" and not d["points"][5]["core_A"]["noisy"]
        whole = s.cal_probe(0, 0, mode=1)
        assert whole.range == 5 and whole.flags & P.CALP_MODE1 and whole.rc == 0
    noisy = sim_factory(signal="sine:freq=10kHz,amp=0.2")

    async def main():
        async with client_for(noisy, tmp_path) as c:
            r = await c.call("scope_cal_probe", channel="CH1", range_index=3)
            assert r["result"] == "NOISE" and r["failure"]["class"] in ("gated", "full rate")
            assert r["text"] == "too much noise" and r["failure"]["value_codes"] > 3
            assert any("stopped for the probe" in x for x in r["caveats"])
            assert (await c.call("scope_status"))["run_state"] == "RUN"
    anyio.run(main)


def test_health_wire_layout_offsets():
    b = P.HealthResp(enc_mode=1, enc_last33=2, enc_flags=3, enc_boot33=4, enc_bad=5, enc_recovered=6,
                     enc_checked=7).pack()
    assert b[88:92] == bytes([1, 2, 3, 4]) and struct.unpack_from("<III", b, 92) == (5, 6, 7)
