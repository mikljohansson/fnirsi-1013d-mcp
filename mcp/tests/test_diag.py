"""Protocol 1.16 (fw 0.2.52, docs/protocol.md row 55): diagnostics readers. GET_HEALTH, the card image identity in
SD_INFO, the calibration provenance in GET_CAL, SAVE_REPORT; the stored layouts against docs/protocol-vectors.json
diag_layouts; the MCP tools scope_health, scope_sd_info (card_image), scope_calibration_info (provenance) and
scope_save_report."""

from __future__ import annotations

import json
import struct
from pathlib import Path

import anyio
import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d.scope import Scope
from fnirsi1013d.simulator import SIM_BUILD, SIM_BUILD_TIME, make_egon_image
from fnirsi1013d.transport import DeviceError

VECTORS = json.loads((Path(__file__).resolve().parents[2] / "docs" / "protocol-vectors.json").read_text())
DIAG = VECTORS["diag_layouts"]


def test_stored_layouts_match_the_firmware_vectors():
    life = bytes.fromhex(DIAG["life"]["hex"])
    d = P.parse_life_record(life)
    want = DIAG["life"]
    assert (d["flags"], d["power_on"], d["minutes"]) == (want["flags"], want["power_on"], want["minutes"])
    assert d["restart"] == want["restart"]
    assert P.pack_life_record(d) == life and len(life) == 64
    assert P.parse_life_record(P.pack_life_record({**d, "restart": None}))["restart"] is None
    with pytest.raises(ValueError, match="no life record"):
        P.parse_life_record(bytes(64))

    prov = bytes.fromhex(DIAG["prov"]["hex"])
    p = P.parse_provenance(prov)
    w = DIAG["prov"]
    assert (p.build, p.version, p.fpga, p.what, int(p.rtc), p.uptime_ms, p.power_on, p.minutes, list(p.time)) == \
        (w["build"], w["version"], w["fpga"], w["what"], w["rtc"], w["uptime_ms"], w["power_on"], w["minutes"],
         w["time"])
    assert P.pack_provenance(p) == prov
    assert p.parts == ["CH1 zeros", "CH1 gains", "CH2 zeros", "CH2 gains"] and p.time_str == "2026-10-19T12:34:56Z"
    with pytest.raises(ValueError, match="CRC"):
        P.parse_provenance(prov[:20] + bytes([prov[20] ^ 1]) + prov[21:])

    ident = bytes.fromhex(DIAG["ident"]["hex"])
    i = P.parse_ident_record(ident)
    w = DIAG["ident"]
    assert (i.version, i.build, list(i.time), list(i.proto)) == (w["version"], w["build"], w["time"], w["proto"])
    assert P.pack_ident_record(i) == ident and len(ident) == 36


def test_response_layouts():
    h = P.HealthResp(usb_state=4, usb_flags=0x1D, usb_power=0x51, usb_faddr=7, suspend_ms=1500, since_config_ms=9,
                     cp15=0x0005107D, power_on=12, minutes=2225, uptime_ms=60000, life_flags=0x0B, last_end=2,
                     settings_flags=1, saves=3, last_save_result=-2, settings_eval_ms=700, rec_flags=3, rec_type=4,
                     rec_restarts=2, rec_power_on=12, rec_exc_lr=0x80012348, rec_far=0xDEADBEEF, rec_minutes=2100)
    b = h.pack()
    assert len(b) == 128 and b[:4] == bytes([3, 0, 128, 0])  # layout 3 since 1.25 (2 / 124 in 1.24)
    assert struct.unpack_from("<BBBBII", b, 4) == (4, 0x1D, 0x51, 7, 1500, 9)
    assert struct.unpack_from("<IIII", b, 16) == (0x0005107D, 12, 2225, 60000)
    assert struct.unpack_from("<BBBBiI", b, 32) == (0x0B, 2, 1, 3, -2, 700)
    assert struct.unpack_from("<BBHII", b, 44) == (3, 4, 2, 12, 0x80012348)
    assert struct.unpack_from("<II", b, 80) == (0xDEADBEEF, 2100) and b[88:] == bytes(40)
    d = h.describe()
    assert d["usb"]["state"] == "suspended" and d["cpu"]["mmu"] and d["cpu"]["dcache"] and d["cpu"]["icache"]
    assert d["restart_record"]["exception"] == "data abort" and d["restart_record"]["from_this_power_on"]
    assert d["settings"]["last_save_result"] == -2 and d["life"]["previous_power_on_ended"].startswith("ended")
    r = P.SaveReportResp(1234, 27, b"reports/report-00012-00.txt")
    assert len(r.pack()) == 56 and P.SaveReportResp.unpack(r.pack()).name_str == "reports/report-00012-00.txt"
    cal = P.CalResp(prov_state=1, prov=bytes.fromhex(DIAG["prov"]["hex"]), prov_power_ons_since=3,
                    prov_minutes_since=125)
    raw = cal.pack()
    assert len(raw) == 432 and raw[288] == 1 and raw[292:348] == cal.prov
    assert struct.unpack_from("<II", raw, 348) == (3, 125)
    back = P.CalResp.unpack(raw)
    assert back.has_provenance and back.provenance().version == "0.2.52"
    assert not P.CalResp.unpack(raw[:288]).has_provenance and P.CalResp.unpack(raw[:288]).provenance() is None
    info = P.SdInfo(1000, 8192, 900, 65536, [P.SdHeaderInfo(s, 0, 0, 0, 0) for s in (16, 80, 750)],
                    P.SdIdent(1, 1, 1, 16, b"0.2.52", 0x1234ABCD, (26, 10, 19, 12, 34, 56), 120000, 41))
    raw = info.pack()
    assert len(raw) == 112 and raw[64:68] == bytes([1, 1, 1, 16]) and raw[68:74] == b"0.2.52"
    assert struct.unpack_from("<I6B", raw, 84) == (0x1234ABCD, 26, 10, 19, 12, 34, 56)
    assert struct.unpack_from("<II", raw, 96) == (120000, 41)
    assert P.SdInfo.unpack(raw).ident.describe().startswith("fw 0.2.52, build 1234abcd, committed 2026-10-19")
    assert P.SdInfo.unpack(raw[:64]).ident is None


def test_health_flags_and_restart_record(sim_factory):
    srv = sim_factory()
    d = srv.device
    d.restart_record = P.parse_life_record(bytes.fromhex(DIAG["life"]["hex"]))["restart"]
    with Scope.connect(srv.url) as s:
        h = s.health()
        assert h.layout == 3 and h.length == 128 and h.power_on == 12 and h.cp15 == 0x0005107D
        assert h.rec_flags == 1 and h.rec_type == 4 and h.rec_far == 0xDEADBEEF
        d.ui_busy = True  # GET_HEALTH answers in any poll context, also with a menu open
        assert s.health(clear_restart=True).rec_flags == 1  # the answer still shows it
        d.ui_busy = False
        assert s.health().rec_flags == 0
        with pytest.raises(DeviceError):
            s.t.request(P.Cmd.GET_HEALTH, bytes([0x02]))
        assert s.info.caps & P.Caps.DEBUG_RAW  # a PC_DEBUG build: the restart test restarts after answering
        d.t_boot -= 100.0
        s.t.request(P.Cmd.GET_HEALTH, bytes([P.HEALTH_RESTART_TEST]))
        assert s.health().uptime_ms < 50_000
    with Scope.connect(sim_factory(caps_clear=P.Caps.DEBUG_RAW).url) as s:
        with pytest.raises(DeviceError) as e:
            s.t.request(P.Cmd.GET_HEALTH, bytes([P.HEALTH_RESTART_TEST]))  # PC_DEBUG builds only
        assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, 0)


def test_card_image_identity_and_cache(sim_factory):
    srv = sim_factory()
    d = srv.device
    with Scope.connect(srv.url) as s:
        assert s.sd_info().ident.state == 0  # never read
        unknown = s.sd_info(identify=True).ident
        assert unknown.state == 4 and unknown.image_length == 300 * 1024
        own = P.ImageIdentity("0.1.4-sim", SIM_BUILD, SIM_BUILD_TIME, (1, 16))
        d.sd.put(750, make_egon_image(200 * 1024 + 100, ident=own))
        assert s.sd_info(identify=True).ident.state == 4  # cached until a write reaches sector 750
        d.ident_cache = None
        i = s.sd_info(identify=True).ident
        assert (i.state, i.running, i.version_str, i.build, i.time, (i.proto_major, i.proto_minor)) == \
            (1, True, "0.1.4-sim", SIM_BUILD, SIM_BUILD_TIME, (1, 16)) and i.ident_ms > 0
        assert s.sd_info(identify=True).ident.ident_ms == 0 and s.sd_info().ident.state == 1  # from the cache
        other = P.ImageIdentity("0.2.51", 0xCAFE, (26, 10, 18, 9, 0, 0), (1, 15))
        d.sd.put(750, make_egon_image(150 * 1024, ident=other))
        d.ident_cache = None
        i = s.sd_info(identify=True).ident
        assert i.state == 1 and not i.running and i.version_str == "0.2.51"
        d.sd.put(750, make_egon_image(100 * 1024, version="0.2.40"))
        d.ident_cache = None
        i = s.sd_info(identify=True).ident
        assert (i.state, i.version_str, i.proto_minor) == (3, "0.2.40", 3)
        d.sd.put(750, bytes(512))
        d.ident_cache = None
        assert s.sd_info(identify=True).ident.state == 5


def test_provenance_and_save_report(sim_factory):
    srv = sim_factory(cal_v3=True)
    d = srv.device
    with Scope.connect(srv.url) as s:
        assert s.get_cal(0, cached=False).prov_state == 0
        d.cal_prov = P.parse_provenance(bytes.fromhex(DIAG["prov"]["hex"]))
        d.power_on, d.minutes_stored = 15, 2400
        c = s.get_cal(1, cached=False)
        assert c.prov_state == 1 and c.prov_power_ons_since == 3 and c.prov_minutes_since == 175
        assert c.provenance().power_on == 12
        d.cal_prov_bad = True
        assert s.get_cal(0, cached=False).prov_state == 2
        r1, r2 = s.save_report(), s.save_report()
        assert (r1.name_str, r2.name_str) == ("reports/report-00015-00.txt", "reports/report-00015-01.txt")
        assert r1.bytes_written == len(d.reports[r1.name_str])
        d.sd.present = False
        with pytest.raises(DeviceError) as e:
            s.save_report()
        assert (e.value.status, e.value.detail) == (P.Status.E_IO, 0)


def test_diagnostics_tools(sim_factory, tmp_path):
    from test_server import client_for

    srv = sim_factory(cal_v3=True)
    d = srv.device
    d.restart_record = P.parse_life_record(bytes.fromhex(DIAG["life"]["hex"]))["restart"]
    d.cal_prov = P.parse_provenance(bytes.fromhex(DIAG["prov"]["hex"]))
    d.sd.put(750, make_egon_image(150 * 1024, ident=P.ImageIdentity("0.2.51", 0xCAFE, (26, 10, 18, 9, 0, 0),
                                                                   (1, 15))))

    async def main():
        async with client_for(srv, tmp_path) as c:
            h = await c.call("scope_health")
            assert h["usb"]["state"] == "configured" and h["life"]["power_on"] == 12
            words = " | ".join(h["in_words"])
            assert "power-on #12" in words and "MMU on" in words
            assert "stored restart record: data abort in power-on #11" in words
            sd = await c.call("scope_sd_info")
            ci = sd["card_image"]
            assert ci["version"] == "0.2.51" and ci["is_running_image"] is False and ci["build"] == "0000cafe"
            assert "the card boots fw 0.2.51" in ci["text"] and "running now: fw 0.1.4-sim" in ci["text"]
            cal = await c.call("scope_calibration_info")
            pv = cal["calibration_provenance"]
            assert pv["state"] == "valid" and pv["fw_version"] == "0.2.52" and pv["fpga"] == "0x1535"
            assert pv["parts"] == ["CH1 zeros", "CH1 gains", "CH2 zeros", "CH2 gains"]
            assert "calibrated by fw 0.2.52" in pv["text"] and "2026-10-19T12:34:56Z" in pv["text"]
            r = await c.call("scope_save_report")
            assert r["file"] == "reports/report-00012-00.txt" and r["bytes"] > 0
            d.sd.present = False
            assert "no SD card" in await c.fails("scope_save_report")
    anyio.run(main)
