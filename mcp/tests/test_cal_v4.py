"""Protocol 1.15 (fw 0.2.50, docs/protocol.md row 54): calibration v4, the scope's background tracking of the
full-rate A - B mismatch (d + e x) and of the full class's common mode cm; GET_CAL 288 bytes; the host volts model
takes both out of READ_RAW and WIDE samples the way the firmware takes them out of its own readouts."""

from __future__ import annotations

import struct
from dataclasses import replace

import anyio
import numpy as np
import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d.calibration import channel_scale
from fnirsi1013d.scope import Scope
from fnirsi1013d.simulator import parse_signal

DRIFT = ((0.9, 0.02, 0.0, 0.75), (-0.5, -0.01, 0.0, -0.6))  # (d, e, q, cm) per channel, codes


def test_get_cal_288_layout_and_older_lengths():
    r = P.CalResp(cal_version=3, track_flags=0x5B, track_d_q8=-230, track_e_q24=335544, track_q_q24=-1678,
                  track_age_ms=1234, track_records=77, track_last_pairs=1900, track_spread16=480, cm_q8=192,
                  cm_probes=9, cm_age_ms=0xFFFFFFFF)
    raw = r.pack()[:288]  # a 1.15 answer (1.16 appends the provenance)
    assert struct.unpack_from("<BxhiiIIHHhHI", raw, 256) == (0x5B, -230, 335544, -1678, 1234, 77, 1900, 480, 192, 9,
                                                              0xFFFFFFFF)
    back = P.CalResp.unpack(raw)
    assert back.has_tracker and not back.has_provenance and back.nbytes == 288
    assert back == P.CalResp.unpack(r.pack()[:288]) and back.track_records == 77 and back.cm_age_ms == 0xFFFFFFFF
    assert back.track_d == -230 / 256 and back.track_e == pytest.approx(0.02, abs=1e-7) and back.track_cm == 0.75
    for n in (256, 248, 72):  # 1.12 / 1.10 / 1.9 answers: no tracker, the tail reads zero
        old = P.CalResp.unpack(raw[:n])
        assert not old.has_tracker and old.track_d == old.track_cm == 0 and old.cal_version == (3 if n > 72 else 0)


def test_scale_v4_round_trips():
    cal = P.CalResp(cal_version=3, cal_flags=P.CAL_LOADED | P.CAL_BOUND, dc_shift_value=1000,
                    gain=(30000, 34000, 32700, 32800), gain_range=2, zero=tuple([128 * 16] * 24))
    cal = P.CalResp.unpack(replace(cal, track_d_q8=256, track_e_q24=1 << 20, cm_q8=128).pack())
    cfg = P.ChannelConfig(range=3, offset_pwm=1000, center_cal=1000, uv_per_lsb_q8=4400000, zero_code_q8=128 * 256)
    cal = replace(cal, dc_calibration_offset=(1000,) * 7)
    sc = channel_scale(0, cfg, cal, None, P.CAL_CLASS_FULL)
    assert (sc.track_d, sc.track_e, sc.track_cm) == (1.0, 1 / 16, 0.5) and "v4 tracking" in sc.source
    assert channel_scale(0, cfg, cal, None, P.CAL_CLASS_GATED).track_cm == 0  # the gated class is untouched
    raw = np.array([20.0, 90.0, 128.0, 170.0, 240.0])
    for core in "AB":
        codes = sc.codes_from_raw(raw, core)  # the scope's v4 codes of these raw codes
        assert np.allclose(sc.volts_compensated(codes, core), sc.volts_raw(raw, core))
    # the v4 code is what the volts say: code - T = volts / volts-per-code (gated reference)
    vpl = cfg.volts_per_lsb
    for core in "AB":
        assert np.allclose(sc.codes_from_raw(raw, core) - 128, sc.volts_raw(raw, core) / vpl)
    m = np.array([100.0, 128.0, 150.0])
    assert np.allclose(sc.volts_wide_codes(sc.codes_mean_raw(m)), sc.volts_mean_raw(m))
    assert np.allclose(sc.codes_mean_raw(m) - 128, sc.volts_mean_raw(m) / (sc._mean_gain / sc.wide_gain))


def _sim(sim_factory, v: float, **kw):
    srv = sim_factory(variant="al3f", cal_v3=True, v4=DRIFT, noise_lsb=0.5, **kw)
    srv.device.cfg.signals = (parse_signal(f"dc:v={v}"), parse_signal(f"dc:v={-v}"))
    return srv


@pytest.mark.parametrize("v", [0.25, -0.25])
def test_full_rate_cores_agree_and_read_true_volts(sim_factory, v):
    """At 200 MSa/s each core of each channel reads the applied voltage, A and B agree, with the tracked d, e and cm
    taken out; the v3-only conversion of the same records is off by the drift (the case 1.15 fixes)."""
    with Scope.connect(_sim(sim_factory, v).url) as s:
        s.set_timebase(divider=0)
        s.set_trigger(mode=0, pretrigger_samples=0, total_samples=8000)
        cap = s.capture(["CH1", "CH2"], keep_raw=True)
        for c, name in enumerate(("CH1", "CH2")):
            ch = cap.channels[name]
            vpl = ch.config.volts_per_lsb
            want = v if c == 0 else -v
            cal = s.get_cal(c)
            assert cal.has_tracker and cal.track_flags & P.TRACK_APPLIES
            sc = ch.scale
            va, vb = sc.volts_raw(ch.raw_a, "A").mean(), sc.volts_raw(ch.raw_b, "B").mean()
            assert abs(va - want) < 0.15 * vpl and abs(vb - want) < 0.15 * vpl, (name, va, vb)
            assert abs(ch.volts.mean() - want) < 0.1 * vpl
            # without the tracker: the cores split by d + e x and both sit cm off
            v3 = channel_scale(c, ch.config, replace(cal, nbytes=256), None, P.CAL_CLASS_FULL)
            d, e, _q, cm = DRIFT[c]
            x = want / vpl
            assert (v3.volts_raw(ch.raw_a, "A").mean() - v3.volts_raw(ch.raw_b, "B").mean()) / vpl == \
                pytest.approx(d + e * x, abs=0.15)
            assert ((v3.volts_raw(ch.raw_a, "A").mean() + v3.volts_raw(ch.raw_b, "B").mean()) / 2 - want) / vpl == \
                pytest.approx(cm, abs=0.15)
        tr = s.read_trace_capture(["CH1"])  # the scope's own corrected codes (v4 applied there)
        assert abs(tr.channels["CH1"].volts.mean() - v) < 0.2 * tr.channels["CH1"].config.volts_per_lsb


def test_filtered_capture_takes_out_the_common_mode(sim_factory):
    with Scope.connect(_sim(sim_factory, 0.25).url) as s:
        s.set_timebase(divider=19)
        s.set_trigger(mode=0, pretrigger_samples=0, total_samples=2000)
        s.set_decim("cic3+fir")
        cal = s.get_cal(0)
        assert cal.has_tracker and not cal.track_flags & P.TRACK_APPLIES  # 8-bit tracker off, cm still applies
        cap = s.capture(["CH1"])
        ch = cap.channels["CH1"]
        vpl = ch.config.volts_per_lsb
        assert abs(ch.volts.mean() - 0.25) < 0.05 * vpl
        v3 = channel_scale(0, ch.config, replace(s.get_cal(0), nbytes=256), None, P.CAL_CLASS_FULL)
        m = ch.samples16.astype(float) * cap.decim.code_per_count
        assert (v3.volts_mean_raw(m).mean() - 0.25) / vpl == pytest.approx(0.75, abs=0.05)
        tr = s.read_trace_capture(["CH1"])
        assert abs(tr.channels["CH1"].volts.mean() - 0.25) < 0.6 * vpl  # 8-bit rounded codes


def test_gated_class_and_conversions_reread_get_cal(sim_factory):
    srv = _sim(sim_factory, 0.25)
    with Scope.connect(srv.url) as s:
        s.set_timebase(divider=99)  # gated class: the stored v3 values alone
        s.set_trigger(mode=0, pretrigger_samples=0, total_samples=2000)
        assert s.get_decim().filter == 0
        cap = s.capture(["CH1"])
        assert cap.channels["CH1"].scale.track_cm == 0 and "v4" not in cap.channels["CH1"].scale.source
        # the tracker moves: a conversion uses GET_CAL at most CAL_TRACK_AGE_S old
        s.set_timebase(divider=0)
        s.get_cal(0)
        srv.device.cfg.v4 = ((0.0, 0.0, 0.0, 0.3), DRIFT[1])
        s._cal_t[0] -= 5.0
        assert s.scale(0, clock_class=P.CAL_CLASS_FULL).track_cm == pytest.approx(0.3, abs=1 / 256)


def test_calibration_info_shows_the_tracker_in_words(sim_factory, tmp_path):
    from test_server import client_for

    async def main():
        async with client_for(_sim(sim_factory, 0.25), tmp_path) as c:
            await c.call("scope_set_timebase", divider=0)
            r = await c.call("scope_calibration_info")
            t = r["scope_calibration"]["CH1"]["tracking"]
            assert t["state"] == "tracking" and t["applies_to_next_8bit_readout"]
            assert t["d_codes"] == pytest.approx(0.9, abs=1 / 256) and t["cm_codes"] == 0.75
            assert t["records"] == 512 and t["cm_probes"] == 6 and t["age_s"] == 0.0
            assert any(w.startswith("tracking: 512 records") for w in t["in_words"])
            assert any(w.startswith("common mode +0.75 code from 6 probes") for w in t["in_words"])
        srv = sim_factory(variant="al3f", cal_v3=True)
        async with client_for(srv, tmp_path) as c:
            t = (await c.call("scope_calibration_info"))["scope_calibration"]["CH1"]["tracking"]
            assert t["state"] == "idle" and t["age_s"] is None
            assert any("no valid class probe" in w for w in t["in_words"])
    anyio.run(main)
