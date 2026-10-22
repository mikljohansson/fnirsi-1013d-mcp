"""Protocol 1.25 (fw 0.2.102): the firmware supports one FPGA bitstream (0x153B). GET_INFO 80 bytes (76..79 the built-for
word and the mismatch flag), GET_HEALTH 128 bytes layout 3 (124..127 the same), the refusal every non-basic command gets
on a mismatch (E_UNSUPPORTED detail 1), the host vectors, ``Profile.mismatch_caveat``, the simulator's ``fpga_version``
and the one UI Average behaviour (GET_STATUS 46 / 47: the MCU's or the sequencer's mean of N records, one planner)."""

from __future__ import annotations

import json
import struct
from pathlib import Path

import anyio
import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d.scope import Profile, Scope
from fnirsi1013d.simulator import build_config, make_parser
from fnirsi1013d.transport import DeviceError

VECTORS = json.loads((Path(__file__).resolve().parents[2] / "docs" / "protocol-vectors.json").read_text())


def frame(name: str) -> dict:
    v = next(x for x in VECTORS["vectors"] if x["name"] == name)
    return v["frames"][0]


def test_constants():
    assert P.PROTO_MINOR >= 25 and P.FW_FPGA_VERSION >= 0x153B and P.FPGA_F_MISMATCH == 0x01
    assert P.UNSUP_FPGA_MISMATCH == 1 and P.FPGA_MISMATCH_TEXT == f"FPGA bitstream mismatch: this firmware needs 0x{P.FW_FPGA_VERSION:04X}"
    # the firmware's F_BASIC set (pc_cmds.c): the link, the diagnostics readers, the card, the screen and FEL
    basic = {P.Cmd.PING, P.Cmd.GET_INFO, P.Cmd.GET_STATUS, P.Cmd.GET_BATTERY, P.Cmd.TEST_PATTERN, P.Cmd.GET_LINK_STATS,
             P.Cmd.GET_BAD_FRAME, P.Cmd.GET_EVENT_LOG, P.Cmd.GET_HEALTH, P.Cmd.READ_SPECTRUM, P.Cmd.SD_INFO,
             P.Cmd.SD_READ_SECTORS, P.Cmd.SD_STAGE, P.Cmd.SD_WRITE_SECTORS, P.Cmd.SAVE_REPORT, P.Cmd.GET_CAL,
             P.Cmd.READ_SCREEN, P.Cmd.FFT_BENCH, P.Cmd.ENTER_FEL}
    assert P.BASIC_CMDS == frozenset(basic) and len(P.BASIC_CMDS) == 19
    for cmd in (P.Cmd.ARM, P.Cmd.SET_TRIGGER, P.Cmd.GET_CHANNEL, P.Cmd.READ_RAW, P.Cmd.SET_REMOTE, P.Cmd.GET_DECIM):
        assert cmd not in P.BASIC_CMDS
    assert P.UI_AVERAGE_NAMES == {0: "off", 1: "mcu", 2: "hardware"}
    assert "detail 1" in P.STATUS_HINTS[P.Status.E_UNSUPPORTED]


def test_info_resp_80_bytes_and_76_byte_decode():
    i = P.InfoResp(fpga_version=0x1532, fw_version=b"0.2.102", fw_fpga_version=0x153B, fpga_flags=P.FPGA_F_MISMATCH)
    b = i.pack()
    assert len(b) == 80 and P.InfoResp.size() == 80
    assert struct.unpack_from("<HBB", b, 76) == (0x153B, 1, 0)
    j = P.InfoResp.unpack(b)
    assert j.fpga_mismatch and (j.fpga_version, j.fw_fpga_version, j.fpga_flags) == (0x1532, 0x153B, 1)
    assert j.fw_version_str == "0.2.102" and j.pack() == b
    old = P.InfoResp.unpack(b[:76])  # firmware up to 1.24: the tail reads 0
    assert (old.fw_fpga_version, old.fpga_flags, old.fpga_mismatch) == (0, 0, False)
    assert old.fpga_version == 0x1532 and old.fw_version_str == "0.2.102"
    assert not P.InfoResp(fw_fpga_version=0x153B).fpga_mismatch


def test_info_and_refusal_vectors():
    ok = P.InfoResp.unpack(bytes.fromhex(frame("host_resp_get_info_1_25")["payload"]))
    assert frame("host_resp_get_info_1_25")["len"] == 80 and ok.proto_minor == 25  # the 1.25 vector
    assert (ok.fpga_version, ok.fw_fpga_version, ok.fpga_flags) == (0x153B, 0x153B, 0) and not ok.fpga_mismatch
    assert ok.caps & P.Caps.CAPTURE_STAGES and ok.caps & P.Caps.SEQ and not ok.caps & P.Caps.INTERLEAVE_UNVERIFIED
    bad = P.InfoResp.unpack(bytes.fromhex(frame("host_resp_get_info_1_25_mismatch")["payload"]))
    assert (bad.fpga_version, bad.fw_fpga_version, bad.fpga_mismatch) == (0x1532, 0x153B, True)
    assert (bad.ring_words, bad.max_total_samples, bad.caps) == (ok.ring_words, ok.max_total_samples, ok.caps)
    f = frame("host_resp_e_unsupported_fpga_mismatch")
    assert f["status"] == P.Status.E_UNSUPPORTED and f["cmd"] == P.Cmd.ARM
    assert P.parse_error_payload(bytes.fromhex(f["payload"])) == \
        (P.UNSUP_FPGA_MISMATCH, "FPGA bitstream mismatch: this firmware needs 0x153B")  # the 1.25 vector


def test_health_resp_128_bytes_layout_3():
    h = P.HealthResp(fw_fpga_version=0x153B, fpga_flags=P.FPGA_F_MISMATCH)
    b = h.pack()
    assert len(b) == 128 and b[0] == 3 and struct.unpack_from("<H", b, 2) == (128,)
    assert struct.unpack_from("<HBB", b, 124) == (0x153B, 1, 0)
    back = P.HealthResp.unpack(b)
    assert back == h and back.has_fpga_version and back.fpga_mismatch and back.has_capture and back.has_enc_check
    d = back.describe()["fpga"]
    assert d["firmware_bitstream"] == "0x153B" and d["mismatch"] and d["text"].startswith("FPGA bitstream mismatch")
    fine = P.HealthResp(fw_fpga_version=0x153B).describe_fpga()
    assert not fine["mismatch"] and "built for" in fine["text"]
    old = P.HealthResp.unpack(P.HealthResp(layout=2, length=124, fpga_flags=1).pack()[:124])  # a 1.24 answer
    assert (old.fw_fpga_version, old.fpga_flags, old.fpga_mismatch, old.has_fpga_version) == (0, 0, False, False)
    assert old.has_capture and old.describe()["fpga"] is None
    v = P.HealthResp.unpack(bytes.fromhex(frame("resp_get_health_1_24")["payload"]))
    assert (v.layout, v.length) == (2, 124) and not v.has_fpga_version and v.describe()["fpga"] is None
    assert (v.fw_fpga_version, v.fpga_flags) == (0, 0)


def test_profile_mismatch_caveat():
    info = P.InfoResp(fpga_version=0x1532, fw_fpga_version=0x153B, fpga_flags=P.FPGA_F_MISMATCH)
    p = Profile(info, "al3", [], info.fw_fpga_version, info.fpga_mismatch)
    text = p.mismatch_caveat()
    assert text is not None and "runs 0x1532" in text and "needs 0x153B" in text and "detail 1" in text
    assert "docs/guide/prepare.md" in text
    assert Profile(P.InfoResp(fpga_version=0x153B, fw_fpga_version=0x153B), "al3", [], 0x153B).mismatch_caveat() is None
    assert Profile(P.InfoResp(fpga_version=0x1532), "al3", []).mismatch_caveat() is None  # firmware before 1.25


def test_ui_average_planner_matches_the_firmware():
    """pc_seq_ui_plan_n / pc_seq_ui_plan / pc_seq_ui_plan_mcu (firmware/scope/host_tests/test_seq.c test_ui_plan)."""
    assert P.seq_ui_plan(1500, 750, 0) == (64, (64 * 30160 + 999) // 1000)
    assert P.seq_ui_plan(1500, 750, 199) == (8, P.seq_ui_plan(1500, 750, 199)[1])
    assert P.seq_ui_plan(1500, 750, 1999) == (0, 0) and P.seq_ui_plan(2047, 750, 0) == (0, 0)
    assert P.seq_ui_plan_n(0) == (0, 0) and P.seq_ui_plan_n(30160) == (64, (64 * 30160 + 999) // 1000)
    assert P.seq_ui_plan_n(20_000_000) == (2, 40000) and P.seq_ui_plan_n(20_000_001) == (0, 0)
    # 3000 16-bit samples at 1 MSa/s (D 199, 1 us per word): 3 ms record + dead + 3.6 ms readout -> 4 records
    n, us = P.seq_ui_plan_mcu(3000, 1500, 199, True)
    assert n == 4 and 4 * 6600 < us <= P.SEQ_UI_BUDGET_US
    # 6000 legacy samples (3000 words) at 200 MSa/s: 30 us + 3.6 ms readout -> 8; 16382 legacy samples -> 4
    assert P.seq_ui_plan_mcu(3000, 1500, 0, False)[0] == 8 and P.seq_ui_plan_mcu(8191, 4095, 0, False)[0] == 4
    # 3000 16-bit samples at 10 kSa/s: 300 ms per record -> single records
    assert P.seq_ui_plan_mcu(3000, 1500, 19999, True) == (0, 0)
    assert P.seq_ui_plan_mcu(1500, 1500, 0, False) == (0, 0) and P.seq_ui_plan_mcu(0, 0, 0, False) == (0, 0)
    # the hardware plan is the same rule without the readout: for a record both could take it averages more
    assert P.seq_ui_plan(1500, 750, 0)[0] >= P.seq_ui_plan_mcu(1500, 750, 0, False)[0]
    for d in range(0, 2000, 37):  # powers of two only, never above the budget
        n, us = P.seq_ui_plan_mcu(3000, 1500, d, True)
        assert n == 0 or (n & (n - 1) == 0 and 2 <= n <= 64 and us <= P.SEQ_UI_BUDGET_US)
    # what GET_STATUS 46 / 47 show: the path and log2 N
    assert P.ui_average_plan(1500, 750, 0, False) == (2, 6)  # legacy, fits the accumulator: hardware, 64
    assert P.ui_average_plan(3000, 1500, 0, False) == (1, 3)  # 3000 legacy words > 2046: MCU, 8
    assert P.ui_average_plan(2047, 750, 0, False) == (1, 4)  # one word over the accumulator: MCU, 16
    assert P.ui_average_plan(3000, 1500, 19, True) == (1, 3)  # 16-bit records: MCU, 8
    assert P.ui_average_plan(3000, 1500, 19999, True) == (1, 0)  # two do not fit: single records
    assert P.ui_average_plan(1500, 750, 1999, False) == (1, 0)  # nor on the hardware path: single records


def test_simulator_ui_average_states(sim_factory):
    srv = sim_factory(signal="sine:freq=10kHz,amp=0.25")
    d = srv.device
    with Scope.connect(srv.url) as s:
        s.set_timebase(divider=0)
        s.set_trigger(pretrigger_samples=1500, total_samples=3000, mode=1)
        assert (s.status().ui_average, s.status().ui_average_log2n) == (0, 0)
        d.ui_average = True
        assert (s.status().ui_average, s.status().ui_average_log2n) == (2, 6)  # the sequencer averages 64
        s.set_trigger(pretrigger_samples=3000, total_samples=6000)  # 3000 words: the MCU sums 8
        st = s.status()
        assert (st.ui_average, st.ui_average_log2n) == (1, 3)
        assert (s.get_decim().ui_average, s.get_decim().ui_average_log2n) == (1, 3)
        s.set_trigger(pretrigger_samples=1500, total_samples=3000)
        s.set_timebase(divider=19)
        s.set_decim("cic2")  # 16-bit records: the MCU, 8
        assert (s.status().ui_average, s.status().ui_average_log2n) == (1, 3)
        s.set_timebase(divider=19999)  # 300 ms per record: single records
        assert (s.status().ui_average, s.status().ui_average_log2n) == (1, 0)
        s.set_trigger(mode=2)  # single trigger mode: the UI does not average
        assert (s.status().ui_average, s.status().ui_average_log2n) == (0, 0)
        d.ui_average = False
        s.set_trigger(mode=0)
        assert s.status().ui_average == 0


def test_simulator_mismatch(sim_factory):
    srv = sim_factory(fpga_version=0x1532, signal="sine:freq=10kHz,amp=0.25")
    with Scope.connect(srv.url) as s:  # connect succeeds: GET_INFO is basic
        i = s.info
        assert (i.fpga_version, i.fw_fpga_version, i.fpga_flags, i.fpga_mismatch) == \
            (0x1532, P.FW_FPGA_VERSION, 1, True)
        assert (i.ring_words, i.max_total_samples) == (8192, 16382) and i.caps & P.Caps.SEQ  # the firmware's fixed ones
        p = s.profile()
        assert p.fpga_mismatch and p.fw_fpga_version == P.FW_FPGA_VERSION
        assert "runs 0x1532" in p.mismatch_caveat() and f"needs 0x{P.FW_FPGA_VERSION:04X}" in p.mismatch_caveat()
        assert s.t.ascii("#VER").endswith("fpga=0x1532")
        h = s.health()
        assert h.fpga_mismatch and (h.layout, h.length, h.fw_fpga_version) == (3, 128, P.FW_FPGA_VERSION)
        assert h.describe()["fpga"]["mismatch"]
        with pytest.raises(DeviceError) as e:
            s.arm()
        assert (e.value.status, e.value.detail, e.value.text) == (P.Status.E_UNSUPPORTED, 1, P.FPGA_MISMATCH_TEXT)
        assert P.FPGA_MISMATCH_TEXT in str(e.value) and "[detail 1]" in str(e.value)
        for call in (lambda: s.set_trigger(level=100), lambda: s.get_channel(0, cached=False),
                     lambda: s.get_decim(), lambda: s.set_gen(enable=True), lambda: s.read_trace(["CH1"])):
            with pytest.raises(DeviceError) as e:
                call()
            assert (e.value.status, e.value.detail) == (P.Status.E_UNSUPPORTED, P.UNSUP_FPGA_MISMATCH)
        assert srv.device.arm_count == 0
        # the basic set still answers
        assert s.ping(b"x") == b"x" and s.status().acq_state == P.AcqState.IDLE
        assert s.link_stats()["frames"] > 0 and s.event_log() is not None
        assert s.get_cal(0, cached=False).nbytes == 432 and s.read_screen().shape == (480, 800)
        assert s.sd_info().card_sectors > 0 and s.battery() is not None


def test_simulator_matching_bitstream_and_legacy_info(sim_factory):
    srv = sim_factory()
    with Scope.connect(srv.url) as s:
        assert not s.info.fpga_mismatch and s.info.fw_fpga_version == P.FW_FPGA_VERSION
        assert len(s.t.request(P.Cmd.GET_INFO).payload) == 80
        assert not s.health().fpga_mismatch and s.health().has_fpga_version
        assert s.arm().acq_state in (P.AcqState.ARMED, P.AcqState.TRIGGERED, P.AcqState.DONE)
    old = sim_factory(proto_minor=24)  # a model of fw 0.2.79..0.2.101: 76 / 124 bytes
    with Scope.connect(old.url) as s:
        assert len(s.t.request(P.Cmd.GET_INFO).payload) == 76 and s.info.fw_fpga_version == 0
        assert (s.health().layout, s.health().length) == (2, 124) and not s.health().has_fpga_version
    legacy = sim_factory(legacy=True)
    with Scope.connect(legacy.url) as s:
        assert len(s.t.request(P.Cmd.GET_INFO).payload) == 76


def test_simulator_cli_fpga_version():
    cfg = build_config(make_parser().parse_args(["--fpga-version", "0x1532"]))
    assert cfg.fpga_version == 0x1532 and cfg.variant == "al3"
    assert build_config(make_parser().parse_args(["--variant", "al3"])).fpga_version == P.FW_FPGA_VERSION
    with pytest.raises(SystemExit):
        make_parser().parse_args(["--variant", "stock"])


def test_server_tools_on_a_mismatch(sim_factory, tmp_path):
    from test_server import client_for

    srv = sim_factory(fpga_version=0x1532, signal="sine:freq=10kHz,amp=0.25")

    async def main():
        async with client_for(srv, tmp_path) as c:
            info = await c.call("scope_info")
            assert info["fpga_version"] == "0x1532"
            h = await c.call("scope_health")
            assert h["ok"]
            assert "FPGA bitstream mismatch" in await c.fails("scope_capture", channels=["CH1"])
    anyio.run(main)
