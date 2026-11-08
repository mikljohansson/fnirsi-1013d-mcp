"""Protocol 1.29 (fw-xzero, docs/protocol.md row 71): each channel's zero moves with the OTHER channel's range relays
(bench 2026-11-08: about -1 / 0 / +1 code in three classes, the coils on); the wizard measures the shift per range and
class, the scope adds it to the raw zeros of every readout, GET_CAL 432..459 reports it and the host's raw-code
conversions (READ_RAW, 16-bit samples) add it for the other channel's range of the capture."""

from __future__ import annotations

import json
import struct
from dataclasses import replace
from pathlib import Path

import anyio
import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d.calibration import channel_scale
from fnirsi1013d.scope import Scope
from fnirsi1013d.simulator import parse_signal

VECTORS = json.loads((Path(__file__).resolve().parents[2] / "docs" / "protocol-vectors.json").read_text("utf-8"))
#: per channel the raw zero shift (codes) with the other channel's relays at 0, 1, 2 coils on (the owner's CH2 at 1 V/div:
#: 127.22 / 128.23 / 129.11; CH1: 126.56 / 127.11 / 128.04)
XZERO = ((-0.55, 0.0, 0.93), (-1.0, 0.0, 0.875))
#: the full clock class (bench 2026-11-08 11:4x: about half the gated shift for two coils on, CH2 at 200 MSa/s)
XZERO_FULL = ((-0.3, 0.0, 0.45), (-0.8, 0.0, 0.5))


def frame_payload(name: str) -> bytes:
    v = next(x for x in VECTORS["vectors"] if x["name"] == name)
    return bytes.fromhex(v["frames"][0]["payload"])


def test_relay_classes_are_the_bench_partition():
    # range 1 alone, ranges 2 and 4, then 0 / 3 / 5 / 6 (the calibration's class): coils on 0, 2, 1
    assert [P.xz_class_of(r) for r in range(7)] == [1, 0, 2, 1, 2, 1, 1]


def test_get_cal_1_29_vector():
    raw = frame_payload("resp_get_cal_1_29")
    assert len(raw) == 480
    c = P.CalResp.unpack(raw)
    assert c.nbytes == 480 and c.has_xz and c.xz_flags == P.XZ_TABLE | P.XZ_APPLIED | P.XZ_FULL
    assert (c.channel, c.xz_range, c.xz_class, c.xz_applied16, c.xz_applied16_full) == (1, 1, 0, -58, -50)
    xz, xzf = VECTORS["adc_cal_xz"]["xz"][1], VECTORS["adc_cal_xz"]["xz_full"][1]
    assert c.xz_table == tuple(v for row in xz for v in row)
    assert c.xz_table_full == tuple(v for row in xzf for v in row)
    assert c.pack() == raw
    # the readout's rule: own range, the other channel's class, the clock class; nothing without v3 in use
    assert c.xz16(3, 1) == 0  # cal_version 0 in the vector: not applied
    v3 = replace(c, cal_version=3)
    assert v3.xz16(3, 1) == -58 and v3.xz16(3, 2) == 62 - 6 and v3.xz16(6, 0) == 0 and v3.xz16(3, None) == 0
    assert v3.xz16(3, 1, P.CAL_CLASS_FULL) == -50 and v3.xz16(3, 2, P.CAL_CLASS_FULL) == 48
    d = v3.describe_xz()
    assert d["measured"] and d["full_class_measured"] and d["per_range_codes"]["3"] == {"0_on": -58 / 16, "2_on": 56 / 16}
    assert d["per_range_codes_full_rate"]["3"] == {"0_on": -50 / 16, "2_on": 48 / 16} and d["shift_now_16bit_codes"] == -50 / 16
    # a 460-byte answer (10fa8d3): the gated table in both classes, as that build applied it
    first = replace(P.CalResp.unpack(raw[:460]), cal_version=3)
    assert first.has_xz and first.xz16(3, 1, P.CAL_CLASS_FULL) == -58 and first.xz_table_full == (0,) * 18
    # a 1.28 answer: no table
    old = P.CalResp.unpack(raw[:432])
    assert not old.has_xz and old.describe_xz() is None and replace(old, cal_version=3).xz16(3, 1) == 0
    assert P.CalResp.unpack(P.CalResp().pack()).describe_xz()["measured"] is False


def test_channel_scale_adds_the_shift_for_the_other_range():
    tab = tuple(v for _r in range(6) for v in (-16, 0, 14))
    cal = P.CalResp(cal_version=3, cal_flags=0x0B, dc_shift_value=385, dc_calibration_offset=(900,) * 7,
                    pwm=(900,) * 6, zero=(2048,) * 24, slope=(-17000,) * 24, gain_range=P.CAL_GAIN_NONE,
                    xz_flags=P.XZ_TABLE | P.XZ_APPLIED | P.XZ_FULL, xz_table=tab,
                    xz_table_full=tuple(v // 2 for v in tab), nbytes=480)
    cfg = P.ChannelConfig(range=3, offset_pwm=900, uv_per_lsb_q8=256 * 34000)
    base = channel_scale(0, cfg, cal, None, P.CAL_CLASS_GATED)
    for other, shift in ((1, -1.0), (2, 0.875), (4, 0.875), (0, 0.0), (3, 0.0), (5, 0.0), (6, 0.0)):
        sc = channel_scale(0, cfg, cal, None, P.CAL_CLASS_GATED, other)
        assert (sc.zero_a - base.zero_a, sc.zero_b - base.zero_b) == (shift, shift)
        # the 0 V code of the corrected codes stays T: comp = T - zero
        assert sc.zero_a + sc.adc_a_comp == base.zero_a + base.adc_a_comp == 128
        assert ("other channel's relays" in sc.source) == bool(shift)
        # the full clock class (200 MSa/s and the 16-bit path) takes the full table
        fb = channel_scale(0, cfg, cal, None, P.CAL_CLASS_FULL)
        sf = channel_scale(0, cfg, cal, None, P.CAL_CLASS_FULL, other)
        assert sf.zero_a - fb.zero_a == shift / 2
    # no table (an older calibration), or not v3: exactly as before
    for c in (replace(cal, xz_flags=0), replace(cal, nbytes=432), replace(cal, cal_version=1)):
        sc = channel_scale(0, cfg, c, None, P.CAL_CLASS_GATED, 1)
        ref = channel_scale(0, cfg, c, None, P.CAL_CLASS_GATED)
        assert (sc.zero_a, sc.zero_b) == (ref.zero_a, ref.zero_b)


def _sim(sim_factory):
    srv = sim_factory(cal_v3=True, xzero=XZERO, xzero_full=XZERO_FULL, noise_lsb=0.5)
    srv.device.cfg.signals = (parse_signal("dc:v=0"), parse_signal("dc:v=0"))
    return srv


@pytest.mark.parametrize("divider, filt", [(19, "off"), (0, "off"), (19, "cic2")])
def test_zero_follows_the_other_channel_in_every_path(sim_factory, divider, filt):
    """0 V reads 0 V through READ_RAW (host model), the 16-bit path and READ_TRACE (corrected on the scope) whatever
    the other channel's range; the conversion without the shift is off by the class's shift."""
    with Scope.connect(_sim(sim_factory).url) as s:
        s.set_timebase(divider=divider)
        s.set_trigger(mode=0, pretrigger_samples=0, total_samples=4000)
        s.set_decim(filt)
        s.set_channel(0, range_index=2)
        for other, cls in ((1, 0), (0, 1), (2, 2), (4, 2), (3, 1)):
            s.set_channel(1, range_index=other)
            cal = s.get_cal(0, cached=False)
            assert cal.has_xz and cal.xz_range == other and cal.xz_class == cls
            full = divider == 0 or filt != "off"
            x = XZERO_FULL if full else XZERO
            shift = 2 * round(8 * (x[0][cls] - x[0][1]))
            assert (cal.xz_applied16 if filt == "off" else cal.xz_applied16_full) == shift
            cap = s.capture(["CH1"])
            ch = cap.channels["CH1"]
            vpl = ch.config.volts_per_lsb
            assert abs(ch.volts.mean()) < 0.1 * vpl, (other, ch.volts.mean() / vpl)
            tr = s.read_trace_capture(["CH1"]).channels["CH1"]
            assert abs(tr.volts.mean()) < (0.6 if filt != "off" else 0.15) * vpl
            # the same record without the table reads the shift
            klass = P.CAL_CLASS_FULL if divider == 0 or filt != "off" else P.CAL_CLASS_GATED
            plain = channel_scale(0, ch.config, replace(cal, xz_flags=0), None, klass)
            if filt == "off":
                raw = s.read_capture(["CH1"], keep_raw=True).channels["CH1"]
                m = (plain.volts_raw(raw.raw_a, "A").mean() + plain.volts_raw(raw.raw_b, "B").mean()) / 2
            else:
                m = plain.volts_mean_raw(ch.samples16.astype(float) * cap.decim.code_per_count).mean()
            assert m / vpl == pytest.approx(shift / 16, abs=0.12)


def test_a_capture_converts_with_the_other_range_it_was_taken_with(sim_factory):
    """The other channel's range at capture time (GET_CAPTURE_INFO) decides the shift, not the live one."""
    with Scope.connect(_sim(sim_factory).url) as s:
        s.set_timebase(divider=19)
        s.set_trigger(mode=0, pretrigger_samples=0, total_samples=4000)
        s.set_channel(0, range_index=2)
        s.set_channel(1, range_index=1)
        s.capture(["CH1"])
        s.set_channel(1, range_index=2)  # live: +0.93; the capture was taken at -0.55
        s.get_cal(0, cached=False)
        ci = s.capture_info()
        assert ci.channels[1].range == 1
        later = s.read_capture(["CH1"], keep_raw=True).channels["CH1"]
        vpl = later.config.volts_per_lsb
        assert abs(later.volts.mean()) < 0.1 * vpl
        assert "-0.500 code" in later.scale.source  # -0.55 in 1/8 code
        live = s.scale(0, later.config, P.CAL_CLASS_GATED)  # GET_CAL's other range: 2
        assert live.zero_a - later.scale.zero_a == pytest.approx(0.875 + 0.5)


def test_older_firmware_and_no_table(sim_factory):
    srv = sim_factory(cal_v3=True, noise_lsb=0.5)
    with Scope.connect(srv.url) as s:
        cal = s.get_cal(0, cached=False)
        assert cal.nbytes == 480 and not cal.has_xz and cal.xz_applied16 == 0
        assert struct.unpack_from("<BBB", cal.pack(), 432)[0] == 0
    srv = sim_factory(cal_v3=True, xzero=XZERO, proto_minor=28)
    with Scope.connect(srv.url) as s:
        cal = s.get_cal(0, cached=False)
        assert cal.nbytes == 432 and not cal.has_xz and s.scale(0).source == "scope calibration v3 (gated clock)"


def test_calibration_info_reports_the_table(sim_factory, tmp_path):
    from test_server import client_for

    async def main():
        async with client_for(_sim(sim_factory), tmp_path) as c:
            await c.call("scope_set_channel", channel="CH2", range_index=1)
            r = await c.call("scope_calibration_info")
            x = r["other_channel_relays"]["CH1"]
            assert x["measured"] and x["other_channel_relays_on"] == 0 and x["shift_now_codes"] == -0.5
            assert x["per_range_codes"]["2"] == {"0_on": -0.5, "2_on": 0.875}
            assert x["per_range_codes_full_rate"]["2"] == {"0_on": -0.25, "2_on": 0.5} and x["full_class_measured"]

    anyio.run(main)
