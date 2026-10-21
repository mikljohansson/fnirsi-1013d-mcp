"""Protocol 1.27 (FPGA 0x153D, docs/plans/fpga-0x153c.md sections 6-7, item D6): the streaming logger (LOG_START /
LOG_READ / LOG_STOP), the generator as the fractional two-step square (SET_GEN P16 / H16, GET_GEN 40 bytes, the
pending handshake), READ_BODE rows on P. Byte layouts as agreed with fw-d4 / fw-d5 (2026-10-21); the pattern
generator, its trigger engine and trigger source 3 were cut (owner 2026-10-21, fw-nopat): their codes are unknown
and source 3 is refused. The simulator's models of all of them."""

from __future__ import annotations

import struct

import numpy as np
import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d.scope import Scope
from fnirsi1013d.simulator import parse_signal
from fnirsi1013d.transport import DeviceError


def gen_sim(sim_factory, ch2: str = "zero", **kw):
    """CH1 probes the calibration post (the 'gen' signal), CH2 a given signal."""
    return sim_factory(signals=(parse_signal("gen"), parse_signal(ch2)), **kw)


# --- wire format ----------------------------------------------------------------------------------------------


def test_constants_caps_and_sizes():
    assert P.PROTO_MINOR == 27 and P.FW_FPGA_VERSION == 0x153D and P.GEN_CLOCK_HZ == 200_000_000
    assert (P.Cmd.LOG_START, P.Cmd.LOG_READ, P.Cmd.LOG_STOP) == (0x0214, 0x0215, 0x0216)
    # no SET_SWEEP (chirps are host-stepped SET_GEN writes); 0x0217 / 0x021A were the cut SET_PATTERN(_TRIGGER)
    assert not any(c in (0x0217, 0x0218, 0x021A) for c in P.Cmd)
    assert (P.Caps.GENERATOR, P.Caps.LOGGER) == (1 << 1, 1 << 23)
    assert not any(c & ((1 << 24) | (1 << 27)) for c in P.Caps)  # reserved: PATTERN_GEN / PATTERN_TRIGGER, cut
    assert P.Features.LOGGER == 0x800 and not any(f & 0x3000 for f in P.Features)
    assert P.AcqState.LOGGING == 5 and P.TRIG_SOURCE_NAMES == ("CH1", "CH2", "EXT")
    assert (P.Gen.size(), P.SetGenReq.size(), P.LogStartReq.size(), P.LogResp.size(), P.LogReadReq.size(),
            P.LogChunkHeader.size(), P.BodeRow.size()) == (40, 20, 16, 48, 8, 16, 20)
    assert P.REQUEST_SIZES[P.Cmd.LOG_START] == 16
    for c in (P.Cmd.LOG_START, P.Cmd.LOG_READ, P.Cmd.LOG_STOP):
        assert P.CMD_CAP[c] == P.Caps.LOGGER
    assert P.CMD_CAP[P.Cmd.SET_GEN] == P.Caps.GENERATOR
    assert P.Cmd.ARM not in P.LOG_ALLOWED and P.Cmd.SET_GEN not in P.LOG_ALLOWED and P.Cmd.LOG_READ in P.LOG_ALLOWED


def test_set_gen_raw_modes_and_gen_layout():
    r = P.SetGenReq(P.SetGenReq.M_FREQ | P.SetGenReq.M_DUTY, 0, 1, 1, 0x0001_2345_6789, 0x0000_ABCD_EF01_2345)
    b = r.pack()
    assert len(b) == 20 and b[:4] == bytes((6, 0, 1, 1))
    assert struct.unpack_from("<QQ", b, 4) == (0x0001_2345_6789, 0x0000_ABCD_EF01_2345)  # P16 at 4, H16 at 12..19
    # duty_mode 0: the u32 duty and the reserved u32 of protocol 1.0-1.26 (bytes unchanged)
    old = P.SetGenReq(P.SetGenReq.M_DUTY, 0, 0, 0, 0, 2500).pack()
    assert struct.unpack_from("<II", old, 12) == (2500, 0)
    g = P.Gen(1, P.GEN_ST_RUNNING | P.GEN_ST_PENDING | P.GEN_ST_STEP, 1_000_000, 5000, P.GEN_ST3_ACTIVE,
              200_000 << 16, 100_000 << 16)
    b = g.pack()
    assert len(b) == 40 and b[0] == 1 and b[1] == 0xA1 and b[2:12] == bytes(10)
    assert struct.unpack_from("<QHBB", b, 12) == (1_000_000, 5000, 1, 0)
    assert struct.unpack_from("<QQ", b, 24) == (200_000 << 16, 100_000 << 16)
    g2 = P.Gen.unpack(b)
    assert g2 == g and g2.pending and g2.running and g2.frequency_hz == 1000.0 and g2.duty == 0.5
    assert g2.steps() == (100_000, 0, 100_000, 0)
    old24 = P.Gen.unpack(b[:24])  # a 1.26 answer: the tail reads 0, the mHz / bp fields still give f and duty
    assert old24.p16 == 0 and old24.frequency_hz == 1000.0


def test_logger_layouts_and_tiers():
    assert P.LogStartReq(3, P.LOG_FMT_RAW8, 799, 60_000).pack() == bytes((3, 0, 0, 0)) + struct.pack("<III", 799,
                                                                                                    60_000, 0)
    lr = P.LogResp(1, 1, 1, P.LOG_F_LAP, 399, 0, 400, 1_000_000, 900_000, 1, 8 << 20, 2000)
    b = lr.pack()
    assert len(b) == 48 and struct.unpack_from("<BBBBIIIQQIII", b) == (1, 1, 1, 1, 399, 0, 400, 1_000_000, 900_000,
                                                                       1, 8 << 20, 2000)
    assert lr.sample_rate == 500e3 and lr.state_name == "running"
    h = P.LogChunkHeader(5, 0xFFFFFFF0, 4, P.LOG_CHUNK_RUNNING | P.LOG_CHUNK_GAP, P.LOG_FMT_WIDE16)
    hb, data = P.parse_log_read(h.pack() + b"\x01\x02\x03\x04")
    assert hb == h and data == b"\x01\x02\x03\x04" and hb.gap and hb.running and not hb.ended
    # the tiers: 500 kS/s one 8-bit channel, 250 kS/s one 16-bit or two 8-bit, 125 kS/s two 16-bit
    assert P.log_check(1, P.LOG_FMT_HIGH8, 399) is None and P.log_sample_rate(P.LOG_FMT_HIGH8, 399) == 500e3
    assert P.log_check(1, P.LOG_FMT_RAW8, 399) is None  # D + 1 = 400 clocks per sample: 500 kS/s
    assert P.log_check(1, P.LOG_FMT_WIDE16, 399)[0] == P.LOG_D_RATE
    assert P.log_check(1, P.LOG_FMT_WIDE16, 799) is None and P.log_check(3, P.LOG_FMT_HIGH8, 799) is None
    assert P.log_check(3, P.LOG_FMT_WIDE16, 799)[0] == P.LOG_D_RATE and P.log_check(3, P.LOG_FMT_PEAK, 1599) is None
    assert P.log_check(0, 1, 399)[0] == P.LOG_D_CHANNELS and P.log_check(1, 4, 399)[0] == P.LOG_D_FORMAT
    assert P.log_check(1, 1, 1 << 20)[0] == P.LOG_D_DIVIDER and P.log_check(1, 1, 0)[0] == P.LOG_D_FORMAT
    assert P.log_word_bytes(3, P.LOG_FMT_RAW8) == 4 and P.log_word_bytes(1, P.LOG_FMT_PEAK) == 2


#: fw-d4's vectors (branch fw-d4 42d322f, docs/protocol-vectors.json): the payloads the firmware produces
FW_D4 = {
    "req_log_start": "010000008f0100000000000000000000",
    "req_log_read": "0000010000000000",
    "req_log_start_2ch_16bit_10s": "030200003f0600001027000000000000",
    "req_log_start_peak": "010300001f0300000000000000000000",
    "resp_log_start_1_27": "010100008f010000640000009001000000000000000000000000000000000000000000000000800000000000"
                           "00000000",
    "resp_log_stop_1_27": "030302033f0600000000000040060000002d310100000000601d310100000000020000000000800"
                          "04c9c000000000000",
    "resp_log_read_1_27_gap": "030000009600000014000000030000001a1b212228292f3036373d3e44454b4c5253595a",
    "resp_log_read_1_27_ended": "07000000921000000000000004010000",
}


def test_logger_structs_match_the_firmware_vectors():
    v = {k: bytes.fromhex(x) for k, x in FW_D4.items()}
    assert P.LogStartReq(1, P.LOG_FMT_RAW8, 399, 0).pack() == v["req_log_start"]
    assert P.LogReadReq().pack() == v["req_log_read"]
    assert P.LogStartReq(3, P.LOG_FMT_WIDE16, 1599, 10_000).pack() == v["req_log_start_2ch_16bit_10s"]
    assert P.LogStartReq(1, P.LOG_FMT_PEAK, 799, 0).pack() == v["req_log_start_peak"]
    assert P.LogResp(1, 1, 0, 0, 399, 100, 400, 0, 0, 0, 8 << 20, 0).pack() == v["resp_log_start_1_27"]
    stop = P.LogResp.unpack(v["resp_log_stop_1_27"])
    assert stop.pack() == v["resp_log_stop_1_27"] and stop.state_name == "stopped"
    assert stop.flags == P.LOG_F_LAP | P.LOG_F_OVERFLOW and stop.sample_rate == 125e3
    h, data = P.parse_log_read(v["resp_log_read_1_27_gap"])
    assert (h.seq, h.word, h.nbytes, h.gap, h.running, len(data)) == (3, 150, 20, True, True, 20)
    h, data = P.parse_log_read(v["resp_log_read_1_27_ended"])
    assert h.ended and not h.running and data == b"" and h.format == P.LOG_FMT_HIGH8


#: fw-d5's vectors (branch fw-d5 bda4607, docs/protocol-vectors.json section 5i)
FW_D5 = {
    "req_set_gen_p16_h16": "06000101ce0ca40d030000006706d28601000000",
    "resp_gen_1_27": "01a1000000000000000000004c400f000000000088130100ce0ca40d030000006706d28601000000",
}


def test_generator_structs_match_the_firmware_vectors():
    v = {k: bytes.fromhex(x) for k, x in FW_D5.items()}
    p16 = P.gen_p16(999_500)
    h16 = P.gen_h16(p16, 5000)
    assert (p16, h16) == (13113756878, 6556878439)
    r = P.SetGenReq(P.SetGenReq.M_FREQ | P.SetGenReq.M_DUTY, 0, 1, 1, p16, h16)
    assert r.pack() == v["req_set_gen_p16_h16"]
    g = P.Gen(1, P.GEN_ST_RUNNING | P.GEN_ST_PENDING | P.GEN_ST_STEP, P.gen_freq_mhz(p16), P.gen_duty_bp(h16, p16),
              P.GEN_ST3_ACTIVE, p16, h16)
    assert g.pack() == v["resp_gen_1_27"] and P.Gen.unpack(v["resp_gen_1_27"]).pending


def test_simulator_answers_the_firmware_vectors(sim_factory):
    """The simulator's answers to fw-d5's requests have the firmware's bytes where they are deterministic."""
    srv = gen_sim(sim_factory, ch2="square:freq=1kHz,amp=0.3")
    v = {k: bytes.fromhex(x) for k, x in FW_D5.items()}
    with Scope.connect(srv.url) as s:
        s.set_gen(enable=True)
        g = P.Gen.unpack(s.t.request(P.Cmd.SET_GEN, v["req_set_gen_p16_h16"]).payload)
        # byte 1: the step in progress and the pending bit (2-3 clocks after the commit) differ
        assert g.pack()[2:] == v["resp_gen_1_27"][2:] and g.running
        for code in (0x0217, 0x021A):  # the cut SET_PATTERN / SET_PATTERN_TRIGGER: unknown, as in the firmware
            with pytest.raises(DeviceError) as e:
                s.t.request(code, bytes(28))
            assert e.value.status == P.Status.E_UNKNOWN_CMD


# --- the simulator through the client -------------------------------------------------------------------------


def test_generator_square_and_the_pending_handshake(sim_factory):
    srv = gen_sim(sim_factory)
    with Scope.connect(srv.url) as s:
        assert s.info.caps & P.Caps.GENERATOR and not s.info.caps & ((1 << 24) | (1 << 27))
        assert s.info.gen_clock_hz == 200_000_000
        g = s.set_gen(enable=True, freq_hz=999.5, duty=0.5)
        assert g.freq_mhz_actual == 999_500 and g.p16 == P.gen_p16(999_500)
        assert g.frequency_hz == pytest.approx(999.5, rel=1e-10) and g.running
        g = s.set_gen(freq_hz=1000.0)  # a change while it runs: taken within 3 clocks (fpga-d3's RTL)
        assert g.steps() == (100_000, 0, 100_000, 0)
        g, polls = s.wait_gen_taken()
        assert not g.pending and polls >= 1
        p16, h16 = P.gen_period_p16(201)
        g = s.set_gen(p16=p16, h16=h16)  # the raw modes: an odd integer period
        assert (g.p16, g.h16, g.steps()) == (201 << 16, 101 << 16, (101, 0, 100, 0))
        assert g.frequency_hz == 200e6 / 201
        for kw, detail in (({"freq_hz": 26e6}, 1), ({"freq_hz": 1e6, "duty": 0.001}, 1), ({"duty": 1.5}, 2)):
            with pytest.raises(DeviceError) as e:
                s.set_gen(**kw)
            assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, detail), kw
        s.set_gen(enable=False)
        assert not s.get_gen().running


def _drain(s: Scope, ch: int, fmt: int) -> tuple[list, int]:
    chunks = list(s.log_chunks(ch, fmt))
    return chunks, sum(c.nwords for c in chunks)


def test_logger_runs_reads_and_refuses(sim_factory):
    srv = sim_factory(signal="sine:freq=1kHz,amp=0.3")
    with Scope.connect(srv.url) as s:
        with pytest.raises(DeviceError) as e:
            s.log_read()
        assert e.value.status == P.Status.E_STATE
        with pytest.raises(DeviceError) as e:  # format 1 needs the filter
            s.log_start(1, P.LOG_FMT_HIGH8, 399)
        assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, P.LOG_D_FORMAT)
        with pytest.raises(DeviceError) as e:
            s.t.request(P.Cmd.LOG_START, P.LogStartReq(1, P.LOG_FMT_RAW8, 199).pack())  # 1 MS/s is over the tier
        assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, P.LOG_D_RATE)
        s.set_trigger(pretrigger_samples=1000, total_samples=4000)
        s.set_decim("cic3+fir")
        r = s.log_start(1, P.LOG_FMT_HIGH8, 399, duration_s=0.3)
        assert r.state_name == "running" and r.sample_rate == 500e3 and r.sample_clocks == 400
        assert s.status().acq_state == P.AcqState.LOGGING
        with pytest.raises(DeviceError) as e:
            s.arm()
        assert (e.value.status, e.value.detail) == (P.Status.E_STATE, P.LOG_E_STATE_LOGGING)
        assert s.get_trigger(cached=False).total_samples == 4000  # read-only GETs are answered
        chunks, words = _drain(s, 1, P.LOG_FMT_HIGH8)
        assert words == pytest.approx(150_000, abs=500) and chunks[-1].ended
        assert all(c.lost_words == 0 for c in chunks)
        assert all(b.word == a.word + a.nwords for a, b in zip(chunks, chunks[1:]))  # contiguous
        x = np.frombuffer(b"".join(c.data for c in chunks), np.uint8).astype(float)
        f = np.fft.rfftfreq(65536, 2e-6)[np.argmax(np.abs(np.fft.rfft(x[:65536] - x[:65536].mean())))]
        assert f == pytest.approx(1000, abs=10)  # the 1 kHz sine at 500 kS/s
        end = s.log_stop()
        assert end.state_name == "ended" and end.sent == end.produced and end.elapsed_ms == 300
        assert s.status().acq_state != P.AcqState.LOGGING
        s.arm()  # acquisition works again


def test_logger_overflow_lap_and_scope_change(sim_factory):
    srv = sim_factory(signal="sine:freq=1kHz,amp=0.3", log_ring_bytes=32768, log_stall_s=0.3)
    with Scope.connect(srv.url) as s:
        s.set_trigger(pretrigger_samples=1000, total_samples=4000)
        s.set_decim("cic3+fir")
        s.log_start(1, P.LOG_FMT_WIDE16, 799, duration_s=0.6)  # 250 kS/s x 2 B: the 32 KiB ring holds 65 ms
        chunks, words = _drain(s, 1, P.LOG_FMT_WIDE16)
        lost = [c for c in chunks if c.lost_words]
        assert lost and lost[0].gap and lost[0].overflow and lost[0].lost_words > 50_000
        assert words + sum(c.lost_words for c in chunks) == pytest.approx(150_000, abs=300)
        end = s.log_stop()
        assert end.flags & P.LOG_F_OVERFLOW and end.gaps >= 1
        srv.device.cfg.log_stall_s, srv.device.cfg.log_ring_bytes = 0.0, 8 << 20
        srv.device.log_lap_next = 1000  # the MCU fell behind the FPGA ring once
        s.log_start(1, P.LOG_FMT_WIDE16, 799, duration_s=0.2)
        chunks, _ = _drain(s, 1, P.LOG_FMT_WIDE16)
        lap = [c for c in chunks if c.lost_words]
        assert lap[0].gap and not lap[0].overflow and lap[0].lost_words > 0
        assert s.log_stop().flags & P.LOG_F_LAP
        s.log_start(1, P.LOG_FMT_WIDE16, 799)  # until stopped; a setting changed at the scope ends it
        srv.device.ui_set_channel(0, range=3)
        assert s.log_stop().state_name == "error"


def test_trigger_source_3_is_refused(sim_factory):
    """Source 3 (the cut pattern's marker) is out of range again: SET_TRIGGER detail 0, SET_TRIGGER2 detail 3."""
    srv = gen_sim(sim_factory)
    with Scope.connect(srv.url) as s:
        with pytest.raises(DeviceError) as e:
            s.set_trigger(source=3)
        assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, 0)
        with pytest.raises(DeviceError) as e:
            s.set_trigger2(type="edge", source=3)
        assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, P.TRIG2_D_SOURCE)
        assert s.get_trigger(cached=False).source == 0


def test_logger_state_refusals_and_idempotent_stop(sim_factory):
    """Row 67 (fw-d4): LOG_START refuses an armed host capture (E_STATE 0) and another view than Scope (10); LOG_STOP
    without a log, or on an ended one, answers LOG as it stands; LOG_READ max_bytes 0 is E_BAD_ARG 0; the generator
    is not changed while logging; the duration is an exact word count."""
    srv = sim_factory(signal="sine:freq=1kHz,amp=0.3")
    with Scope.connect(srv.url) as s:
        assert s.log_stop().state_name == "idle"
        s.set_trigger(mode=1, level=250)
        s.arm()
        with pytest.raises(DeviceError) as e:
            s.log_start(1, P.LOG_FMT_RAW8, 399)
        assert (e.value.status, e.value.detail) == (P.Status.E_STATE, P.LOG_S_ARMED)
        s.stop()
        srv.device.ui_set_spectrum(True)
        with pytest.raises(DeviceError) as e:
            s.log_start(1, P.LOG_FMT_RAW8, 399)
        assert (e.value.status, e.value.detail) == (P.Status.E_STATE, P.LOG_S_VIEW)
        srv.device.ui_set_spectrum(False)
        s.log_start(1, P.LOG_FMT_RAW8, 399, duration_s=0.1)
        with pytest.raises(DeviceError) as e:
            s.set_gen(enable=True)
        assert (e.value.status, e.value.detail) == (P.Status.E_STATE, P.LOG_E_STATE_LOGGING)
        with pytest.raises(DeviceError) as e:
            s.t.request(P.Cmd.LOG_READ, P.LogReadReq(0).pack())
        assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, 0)
        _, words = _drain(s, 1, P.LOG_FMT_RAW8)
        assert words == 25_000  # ceil(100 ms x 200,000 / 800 clocks per word)
        a = s.log_stop()
        assert a.state_name == "ended" and s.log_stop() == a
