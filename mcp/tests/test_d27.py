"""Protocol 1.27 (FPGA 0x153D, docs/plans/fpga-0x153c.md sections 6-7, item D6): the streaming logger (LOG_START /
LOG_READ / LOG_STOP), the generator as the two-step pattern (SET_GEN P16 / H16, GET_GEN 40 bytes, the pending
handshake), the pattern generator (SET_PATTERN) with its marker as trigger source 3, the pattern trigger engine
(SET_PATTERN_TRIGGER), READ_BODE rows on P. Byte layouts as agreed with fw-d4 / fw-d5 (2026-10-21); the simulator's
models of all of them."""

from __future__ import annotations

import struct

import numpy as np
import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d.scope import Scope
from fnirsi1013d.simulator import PatternState, parse_signal
from fnirsi1013d.transport import DeviceError


def gen_sim(sim_factory, ch2: str = "zero", **kw):
    """CH1 probes the calibration post (the 'gen' signal), CH2 a given signal."""
    return sim_factory(signals=(parse_signal("gen"), parse_signal(ch2)), **kw)


# --- wire format ----------------------------------------------------------------------------------------------


def test_constants_caps_and_sizes():
    assert P.PROTO_MINOR == 27 and P.FW_FPGA_VERSION == 0x153D and P.GEN_CLOCK_HZ == 200_000_000
    assert (P.Cmd.LOG_START, P.Cmd.LOG_READ, P.Cmd.LOG_STOP, P.Cmd.SET_PATTERN, P.Cmd.SET_PATTERN_TRIGGER) == \
        (0x0214, 0x0215, 0x0216, 0x0217, 0x021A)
    assert not any(c == 0x0218 for c in P.Cmd)  # no SET_SWEEP: chirps are host-stepped SET_GEN writes
    assert (P.Caps.GENERATOR, P.Caps.LOGGER, P.Caps.PATTERN_GEN, P.Caps.PATTERN_TRIGGER) == \
        (1 << 1, 1 << 23, 1 << 24, 1 << 27)
    assert (P.Features.LOGGER, P.Features.PATTERN, P.Features.PATTERN_TRIGGER) == (0x800, 0x1000, 0x2000)
    assert P.AcqState.LOGGING == 5 and P.TRIG_SOURCE_PATTERN == 3
    assert (P.Gen.size(), P.SetGenReq.size(), P.LogStartReq.size(), P.LogResp.size(), P.LogReadReq.size(),
            P.LogChunkHeader.size(), P.PatternHeader.size(), P.PatternStep.size(), P.BodeRow.size()) == \
        (40, 20, 16, 48, 8, 16, 12, 8, 20)
    assert len(P.PatternTriggerReq().pack()) == 28 and len(P.PatternTriggerResp().pack()) == 32
    assert P.REQUEST_SIZES[P.Cmd.SET_PATTERN] == 12 and P.REQUEST_SIZES[P.Cmd.LOG_START] == 16
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
    g = P.Gen(1, P.PAT_ST_RUNNING | P.PAT_ST_PENDING | 1, 7, 1_000_000, 5000, P.PAT_ST3_ACTIVE, 0,
              200_000 << 16, 100_000 << 16)
    b = g.pack()
    assert len(b) == 40 and b[0] == 1 and b[1] == 0xA1 and struct.unpack_from("<H", b, 2) == (7,)
    assert b[4:12] == bytes(8) and struct.unpack_from("<QHBB", b, 12) == (1_000_000, 5000, 1, 0)
    assert struct.unpack_from("<QQ", b, 24) == (200_000 << 16, 100_000 << 16)
    g2 = P.Gen.unpack(b)
    assert g2 == g and g2.pending and g2.running and g2.frequency_hz == 1000.0 and g2.duty == 0.5
    assert g2.steps() == (100_000, 0, 100_000, 0)
    old24 = P.Gen.unpack(b[:24])  # a 1.26 answer: the tail reads 0, the mHz / bp fields still give f and duty
    assert old24.p16 == 0 and old24.frequency_hz == 1000.0


def test_pattern_layouts():
    steps = [P.PatternStep.from_seconds(50e-6, 1, True), P.PatternStep.from_seconds(950.0025e-6, 0)]
    assert (steps[0].duration, steps[0].fraction, steps[0].flags) == (10_000, 0, 3)
    assert steps[1].duration == 190_000 and steps[1].fraction == 32768  # half a clock
    req = P.SetPatternReq(P.PatternHeader(P.PAT_REPEAT, P.PAT_F_MARKER_TRIGGER | P.PAT_START_BOTH << 6, 10, 1000, 0,
                                          1, 1), steps)
    b = req.pack()
    assert len(b) == 12 + 16
    assert b[:12] == bytes((2, 0x88)) + struct.pack("<HIBBBB", 10, 1000, 2, 1, 1, 0)
    assert b[12:20] == bytes((3, 0)) + struct.pack("<HI", 0, 10_000)
    assert b[20:28] == bytes((0, 0)) + struct.pack("<HI", 32768, 190_000)
    back = P.SetPatternReq.unpack(b)
    assert back.header.n == 2 and back.steps == steps and back.header.start_source == P.PAT_START_BOTH
    resp = P.PatternResp(back.header, P.PAT_ST_RUNNING | 1, P.PAT_ST3_ACTIVE, 3, P.pattern_burst16(steps), steps)
    rb = resp.pack()
    assert len(rb) == 24 + 16 and rb[12:24] == struct.pack("<BBHQ", 0x81, 1, 3, (200_000 << 16) + 32768)
    assert P.PatternResp.unpack(rb).to_dict()["burst_s"] == pytest.approx(1.0000025e-3)
    t2 = P.Trigger2(P.TRIG_NTH_EDGE, 0, 0, 1, 3, 0, 140, 0, 0, 0)
    pt = P.PatternTriggerReq(t2, 1, 0).pack()
    assert pt == t2.pack() + bytes((1, 0, 0, 0))
    pr = P.PatternTriggerResp(t2, 1, 1, P.PTRIG_ST_OWN | P.PTRIG_ST_EVENT | 2, 2).pack()
    assert pr[24:] == bytes((1, 1, 0, 0, 0xC2, 0)) + struct.pack("<H", 2)
    with pytest.raises(ValueError):
        P.PatternStep.from_seconds(5e-9, 1)  # one clock: shorter than 2


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
    "resp_gen_1_27": "0120000000000000000000004c400f000000000088130100ce0ca40d030000006706d28601000000",
    "req_set_pattern_pulse_train": "02080a00000000000201010003000000102700000000000030e60200",
    "resp_set_pattern": "02080a000000000002010100800100000000400d0300000003000000102700000000000030e60200",
    "req_set_pattern_trigger_nth_ch2": "0200000103000000e80300008c000000000000000000000001010000",
    "resp_set_pattern_trigger": "0200000103000000e80300008c000000000000000000000001010000a2000200",
    "req_set_trigger2_marker": "000000030000000000000000800000000000000000000000",
}


def test_generator_and_pattern_structs_match_the_firmware_vectors():
    v = {k: bytes.fromhex(x) for k, x in FW_D5.items()}
    p16 = P.gen_p16(999_500)
    h16 = P.gen_h16(p16, 5000)
    assert (p16, h16) == (13113756878, 6556878439)
    r = P.SetGenReq(P.SetGenReq.M_FREQ | P.SetGenReq.M_DUTY, 0, 1, 1, p16, h16)
    assert r.pack() == v["req_set_gen_p16_h16"]
    g = P.Gen(1, P.PAT_ST_PENDING, 0, P.gen_freq_mhz(p16), P.gen_duty_bp(h16, p16), P.PAT_ST3_ACTIVE,
              P.GEN_SOURCE_SQUARE, p16, h16)
    assert g.pack() == v["resp_gen_1_27"] and P.Gen.unpack(v["resp_gen_1_27"]).pending
    steps = [P.PatternStep.from_seconds(50e-6, 1, True), P.PatternStep.from_seconds(950e-6, 0)]
    hdr = P.PatternHeader(P.PAT_REPEAT, P.PAT_F_MARKER_TRIGGER, 10, 0, 2, 1, 1)
    assert P.SetPatternReq(hdr, steps).pack() == v["req_set_pattern_pulse_train"]
    resp = P.PatternResp(hdr, P.PAT_ST_RUNNING, P.PAT_ST3_ACTIVE, 0, P.pattern_burst16(steps), steps)
    assert resp.pack() == v["resp_set_pattern"] and P.PatternResp.unpack(v["resp_set_pattern"]) == resp
    t2 = P.Trigger2(P.TRIG_NTH_EDGE, 0, 0, P.TRIG_SOURCE_CH2, 3, 1000, 140, 0, 0, 0)
    assert P.PatternTriggerReq(t2, 1, 1).pack() == v["req_set_pattern_trigger_nth_ch2"]
    pr = P.PatternTriggerResp(t2, 1, 1, P.PTRIG_ST_OWN | P.STATUS34_COUNTING | P.TRIG_NTH_EDGE, 2)
    assert pr.pack() == v["resp_set_pattern_trigger"]
    assert P.Trigger2(source=P.TRIG_SOURCE_PATTERN).pack() == v["req_set_trigger2_marker"]


def test_simulator_answers_the_firmware_vectors(sim_factory):
    """The simulator's answers to fw-d5's requests have the firmware's bytes where they are deterministic."""
    srv = gen_sim(sim_factory, ch2="square:freq=1kHz,amp=0.3")
    v = {k: bytes.fromhex(x) for k, x in FW_D5.items()}
    with Scope.connect(srv.url) as s:
        s.set_gen(enable=True)
        g = P.Gen.unpack(s.t.request(P.Cmd.SET_GEN, v["req_set_gen_p16_h16"]).payload)
        assert g.pack()[2:] == v["resp_gen_1_27"][2:] and g.pending  # byte 1: the step in progress may differ
        r = s.t.request(P.Cmd.SET_PATTERN_TRIGGER, v["req_set_pattern_trigger_nth_ch2"]).payload
        assert r == v["resp_set_pattern_trigger"]
        r = s.t.request(P.Cmd.SET_PATTERN, v["req_set_pattern_pulse_train"]).payload
        assert r[:12] + r[13:] == v["resp_set_pattern"][:12] + v["resp_set_pattern"][13:]  # 12: the step now
        assert s.t.request(P.Cmd.SET_TRIGGER2, v["req_set_trigger2_marker"]).payload[28:] == \
            v["req_set_trigger2_marker"]


# --- the pattern model (contract 7.2)----------------------------------------------------------------------------


def test_pattern_model_fraction_accumulator():
    """3.5 + 2.25 clock steps (the fpga-spec prototype): one accumulator puts every edge within one clock of its
    ideal time and the mean period is exact; a burst restarts the accumulator."""
    pat = PatternState(P.PAT_CONTINUOUS, 0, 1, 0, ((1, 1, 3 * 65536 + 32768), (0, 0, 2 * 65536 + 16384)))
    c = np.arange(0, 6000)
    lv = pat.levels(c)
    rise = np.flatnonzero(np.diff(lv) == 1) + 1
    ideal = np.round(rise / 5.75) * 5.75
    assert np.max(np.abs(rise - ideal)) <= 1.0
    assert (rise[-1] - rise[0]) / (len(rise) - 1) == pytest.approx(5.75, abs=0.01)
    marks = pat.mark_clocks(0, 6000)
    assert np.array_equal(marks[1:], rise[:len(marks) - 1]) and marks[0] == 0
    burst = PatternState(P.PAT_REPEAT, 0, 3, 10, ((1, 0, 3 * 65536 + 32768), (0, 0, 2 * 65536 + 16384)))
    lv = burst.levels(np.arange(-5, 60))
    assert lv[:5].tolist() == [0] * 5 and burst.train_clocks() == 3 * (5 + 10) - 10
    assert np.array_equal(burst.levels(np.arange(0, 5)), burst.levels(np.arange(15, 20)))  # accumulator cleared
    assert burst.state_at(16)[:2] == (True, False) and burst.state_at(7)[1] is True and burst.state_at(99)[0] is False


# --- the simulator through the client -------------------------------------------------------------------------


def test_generator_on_the_pattern_and_the_pending_handshake(sim_factory):
    srv = gen_sim(sim_factory)
    with Scope.connect(srv.url) as s:
        assert s.info.caps & P.Caps.PATTERN_GEN and s.info.gen_clock_hz == 200_000_000
        g = s.set_gen(enable=True, freq_hz=999.5, duty=0.5)
        assert g.source == P.GEN_SOURCE_SQUARE and g.freq_mhz_actual == 999_500 and g.p16 == P.gen_p16(999_500)
        assert g.frequency_hz == pytest.approx(999.5, rel=1e-10) and g.running
        g = s.set_gen(freq_hz=1000.0)  # a change while it runs: pending until the next step boundary
        assert g.pending and g.steps() == (100_000, 0, 100_000, 0)
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


def test_set_pattern_refusals(sim_factory):
    srv = gen_sim(sim_factory)
    with Scope.connect(srv.url) as s:
        st = [P.PatternStep.from_seconds(1e-6, 1), P.PatternStep.from_seconds(1e-6, 0)]
        good = P.SetPatternReq(P.PatternHeader(P.PAT_SINGLE, 0, 1, 0, 2, 1, 0), st).pack()
        cases = [(bytes((4,)) + good[1:], P.Status.E_BAD_ARG, 0), (good[:1] + bytes((0x02,)) + good[2:],
                                                                   P.Status.E_BAD_ARG, 1),
                 (good[:1] + bytes((0xC0,)) + good[2:], P.Status.E_BAD_ARG, 1),
                 (bytes((3, 0x40)) + good[2:], P.Status.E_BAD_ARG, 1),  # mode 3 with the engine as the only start
                 (good[:8] + bytes((9,)) + good[9:], P.Status.E_BAD_ARG, 8),
                 (good[:9] + bytes((2,)) + good[10:], P.Status.E_BAD_ARG, 9),
                 (bytes((0, 0)) + good[2:10] + bytes((1,)) + good[11:], P.Status.E_BAD_ARG, 10),
                 (good[:12] + bytes((4,)) + good[13:], P.Status.E_BAD_ARG, 12),
                 (good[:20] + bytes((0, 0, 0, 0, 1, 0, 0, 0)), P.Status.E_BAD_ARG, 24),
                 (good[:-8], P.Status.E_BAD_LEN, 0)]
        for payload, status, detail in cases:
            with pytest.raises(DeviceError) as e:
                s.t.request(P.Cmd.SET_PATTERN, payload)
            assert (e.value.status, e.value.detail) == (status, detail), payload.hex()
        s.set_trigger(source=2)  # EXT: the pin is the input, a driven pattern is refused, a hi-Z one runs
        with pytest.raises(DeviceError) as e:
            s.set_pattern(st, mode="single")
        assert (e.value.status, e.value.detail) == (P.Status.E_STATE, 0)
        r = s.set_pattern(st, mode="continuous", hiz=True, marker_trigger=True)
        assert r.running and s.get_gen().enable == 0 and s.get_gen().source == P.GEN_SOURCE_PATTERN
        with pytest.raises(DeviceError) as e:  # the marker is an edge source
            s.set_trigger2(type="pulse_width", n=10, source=3)
        assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, P.TRIG2_D_SOURCE)


def test_marker_trigger_captures_the_pulse(sim_factory):
    """The contract's TX case: arm on the marker (source 3), then start a 50 us pulse every 1 ms: the capture shows
    the pulse at the pretrigger point, the GEN status counts the bursts."""
    srv = gen_sim(sim_factory)
    with Scope.connect(srv.url) as s:
        s.set_channel(0, range_index=1)
        s.set_timebase(divider=199)  # 1 MSa/s
        s.set_trigger(source=3, edge=0, mode=1, pretrigger_samples=200, total_samples=4000)
        steps = [P.PatternStep.from_seconds(50e-6, 1, True), P.PatternStep.from_seconds(950e-6, 0)]
        s.set_pattern(steps, mode="repeat", repeat=10, marker_trigger=True)
        s.arm()
        r = s.set_pattern(steps, mode="repeat", repeat=10, marker_trigger=True, start=True)
        assert r.burst_s == pytest.approx(1e-3)
        st = s.wait(2.0)
        assert st.acq_state == P.AcqState.DONE
        cap = s.read_capture(["CH1"])
        v = cap.channels["CH1"].volts
        high = v > 1.5
        first = int(np.argmax(high))
        assert abs(first - 200) <= 2 and high[first:first + 45].all() and not high[first + 60:first + 900].any()
        assert np.count_nonzero(np.diff(high.astype(int)) == 1) == 4  # 4 ms after the trigger: 4 pulses
        assert s.get_gen().pat_status3 & P.PAT_ST3_MARKER
        # the marker without SET_PATTERN's marker flag never triggers: auto mode times out
        s.set_pattern(steps, mode="continuous", marker_trigger=False)
        s.set_trigger(mode=0)
        assert s.capture(["CH1"], timeout_s=2.0).auto_timeout


def test_pattern_trigger_engine_starts_bursts(sim_factory):
    """The engine on CH2 Nth edge 3 (a 1 kHz square) starts a 20 us pulse one word after every third edge."""
    srv = gen_sim(sim_factory, ch2="square:freq=1kHz,amp=0.3")
    with Scope.connect(srv.url) as s:
        s.set_channel(0, range_index=1)
        s.set_channel(1, range_index=4)
        s.set_timebase(divider=199)
        s.set_trigger(source=0, edge=0, level=150, mode=1, pretrigger_samples=1000, total_samples=12000)
        pt = s.set_pattern_trigger(type="nth_edge", polarity="rising", source="CH2", n=3, level=128, rearm=True)
        assert pt.own_engine and pt.armed and not pt.event_seen and pt.edges_left == 2
        s.set_pattern([P.PatternStep.from_seconds(20e-6, 1), P.PatternStep.from_seconds(1e-6, 0)], mode="single",
                      start_source="trigger")
        cap = s.capture(["CH1", "CH2"], timeout_s=3)
        hi = np.flatnonzero(np.diff((cap.channels["CH1"].volts > 1.5).astype(int)) == 1) + 1
        e2 = np.flatnonzero(np.diff((cap.channels["CH2"].volts > 0).astype(int)) == 1) + 1
        assert len(hi) == 4 and np.allclose(np.diff(hi) * cap.dt, 3e-3, atol=4e-6)
        for h in hi:  # each pulse follows a CH2 rising edge within a few words
            assert np.min(np.abs(e2 - h)) <= 4
        pt = s.set_pattern_trigger(type="nth_edge", source="CH2", n=3, armed=False)
        with pytest.raises(DeviceError) as e:
            s.t.request(P.Cmd.SET_PATTERN_TRIGGER, P.PatternTriggerReq(P.Trigger2(holdoff=5), 1, 1).pack())
        assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, 16)
        with pytest.raises(DeviceError) as e:
            s.t.request(P.Cmd.SET_PATTERN_TRIGGER, P.PatternTriggerReq(P.Trigger2(source=3), 1, 1).pack())
        assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, P.TRIG2_D_SOURCE)


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


def test_trigger_source_pattern_needs_the_edge_type(sim_factory):
    srv = gen_sim(sim_factory)
    with Scope.connect(srv.url) as s:
        s.set_trigger2(type="pulse_width", n=10)
        with pytest.raises(DeviceError) as e:
            s.set_trigger(source=3)
        assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, 0)
        s.set_trigger2(type="edge", source=3)
        assert s.get_trigger(cached=False).source == 3


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
