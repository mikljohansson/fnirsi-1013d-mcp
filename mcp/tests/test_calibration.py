"""Per-unit, per-core voltage calibration (T8): fit, conversion, the T8 importer and scope_calibrate_volts."""

from __future__ import annotations

import json

import numpy as np
import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d.calibration import UnitCalibration, channel_scale, fw_slope, import_t8
from fnirsi1013d.scope import Scope
from fnirsi1013d.simulator import parse_signal

from test_server import client_for

# The lead's T8 rows (fw 0.1.7, CH1, 1x, DC, dcoffset 44), ranges 0 and 3, at 0.010 V and 1.000 V
T8 = {0.010: {0: (904, 121.08, 116.13, 43146069), 3: (903, 121.48, 116.54, 4465117), 5: (903, 123.71, 118.64, 882759)},
      1.000: {0: (904, 127.01, 121.95, 43146069), 3: (903, 178.42, 173.09, 4465117), 5: (903, 255.0, 255.0, 882759)}}


def t8_files(tmp_path):
    files = []
    for v, rows in T8.items():
        f = tmp_path / f"t8_CH1_{v:.3f}V.json"
        f.write_text(json.dumps({"fw": "0.1.7", "channel": "CH1", "applied_volts": v, "cal": "CalResp(channel=0, "
                                 "adc_a_comp=-2, adc_b_comp=2, dc_shift_center=174, dc_shift_size=220, "
                                 "dc_shift_value=385)",
                                 "rows": [{"range": r, "offset_pwm": pwm, "meanA": a, "meanB": b, "uv_per_lsb_q8": q,
                                           "probe_index": 1} for r, (pwm, a, b, q) in rows.items()]}))
        files.append(f)
    return files


class Cfg:
    def __init__(self, rng, pwm, q8, zero_q8=29843, probe_index=1):
        self.range, self.offset_pwm, self.uv_per_lsb_q8, self.zero_code_q8, self.probe_index = rng, pwm, q8, zero_q8, \
            probe_index

    volts_per_lsb = property(lambda s: s.uv_per_lsb_q8 / 256 * 1e-6)
    zero_code = property(lambda s: s.zero_code_q8 / 256)
    probe_factor = property(lambda s: P.PROBE_FACTORS[s.probe_index])


class Cal:
    adc_a_comp, adc_b_comp, dc_shift_value = -2, 2, 385


def test_t8_import_fits_per_core_zero_and_gain(tmp_path):
    cal = import_t8(t8_files(tmp_path))
    a0, b0 = cal.core_fit(0, 0, "A"), cal.core_fit(0, 0, "B")
    assert a0.zero == pytest.approx(121.02, abs=0.01) and b0.zero == pytest.approx(116.07, abs=0.01)
    assert a0.gain == pytest.approx(0.16695, rel=1e-3) and b0.gain == pytest.approx(0.17010, rel=1e-3)
    a5 = cal.core_fit(0, 5, "A")  # 1.000 V clips on range 5: zero from 0.010 V with the firmware gain
    assert a5.n == 1 and not a5.gain_measured and a5.zero == pytest.approx(123.71 - 0.010 / (882759 / 256e6), abs=1e-3)
    assert not a0.slope_measured  # one trace offset only: the firmware slope is used
    # both applied voltages come back within 2 mV on range 3 (17.4 mV/LSB), per core and on the mean
    sc = channel_scale(0, Cfg(3, 903, 4465117), Cal(), cal)
    assert sc.source == "unit-calibration"
    for v, (_, a, b, _) in ((v, T8[v][3]) for v in T8):
        va, vb = sc.volts_raw(np.array([a]), "A")[0], sc.volts_raw(np.array([b]), "B")[0]
        assert va == pytest.approx(v, abs=2e-3) and vb == pytest.approx(v, abs=2e-3)
    # the firmware formula is 0.042 V off at 0.010 V on range 3, as the client reported on the bench
    fw = channel_scale(0, Cfg(3, 903, 4465117), Cal(), None)
    fwv = (fw.volts_raw(np.array([121.48]), "A")[0] + fw.volts_raw(np.array([116.54]), "B")[0]) / 2
    assert fwv == pytest.approx(0.0425, abs=2e-3)


def test_save_load_and_slope_fit(tmp_path):
    cal = UnitCalibration(unit="u")
    for pwm, a in ((900, 120.0), (1000, 104.0)):  # 0 V at two trace offsets: -0.16 codes per count
        cal.add_point(channel=1, range_index=2, offset_pwm=pwm, volts=0.0, meanA=a, meanB=a - 5, fw_volts_per_lsb=0.0345,
                      dc_shift_value=385)
    cal.add_point(channel=1, range_index=2, offset_pwm=950, volts=1.0, meanA=112 + 1 / 0.0345, meanB=107 + 1 / 0.0345,
                  fw_volts_per_lsb=0.0345, dc_shift_value=385)
    cal.refit()
    f = cal.core_fit(1, 2, "A")
    assert f.slope_measured and f.slope == pytest.approx(-0.16, abs=1e-6) and f.gain == pytest.approx(0.0345, rel=1e-6)
    assert f.zero_at(925, fw_slope(385)) == pytest.approx(116.0, abs=1e-6)
    p = cal.save(tmp_path / "cal" / "u.json")
    back = UnitCalibration.load(p)
    assert back.core_fit(1, 2, "B").zero_at(1000, 0) == pytest.approx(99.0, abs=1e-6)
    assert channel_scale(1, Cfg(4, 900, 1745455), Cal(), back).source == "firmware"  # range 4 not covered


@pytest.fixture
def anyio_backend():
    return "asyncio"


def bad_unit(sim_factory, v=0.0):
    """A unit whose front end misses the firmware formula like the owner's: per-core zero errors and a weaker
    offset-PWM slope."""
    return sim_factory(signal=f"dc:v={v}", zero_error=((-7.0, -9.0), (0.0, 0.0)), offset_slope=-0.16,
                       noise_lsb=0.7)


def set_dc(srv, v):
    srv.device.cfg.signals = (parse_signal(f"dc:v={v}"), srv.device.cfg.signals[1])


@pytest.mark.anyio
async def test_calibrate_volts_tool_end_to_end(sim_factory, tmp_path):
    srv = bad_unit(sim_factory)
    async with client_for(srv, tmp_path) as c:
        await c.call("scope_set_channel", channel="CH1", range_index=3, coupling="DC")
        before = (await c.call("scope_measure", items=["mean"], capture=True))["channels"]["CH1"]["mean"]
        assert abs(before) > 0.08  # firmware formula: 0 V reads ~0.1 V off on range 3
        r = await c.call("scope_calibrate_volts", channel="CH1", applied_volts=0.0, ranges=[2, 3, 4])
        assert [m["range"] for m in r["measured"]] == [2, 3, 4] and any("one voltage" in x for x in r["caveats"])
        set_dc(srv, 1.0)
        r = await c.call("scope_calibrate_volts", channel="CH1", applied_volts="1V", ranges=[2, 3, 4])
        assert [s["range"] for s in r["skipped"]] == [4] and r["skipped"][0]["reason"] == "clipped"
        # a second trace offset fits the PWM slope
        await c.call("scope_set_channel", channel="CH1", offset_raw=1060)
        set_dc(srv, 0.0)
        await c.call("scope_calibrate_volts", channel="CH1", applied_volts=0, ranges=[3])
        assert srv.device.settings.channels[0].range == 3  # restored
        f = c.sess.scope.unit_cal.core_fit(0, 3, "A")
        assert f.slope_measured and f.slope == pytest.approx(-0.16, abs=0.03)
        # now readings are right at a third offset and another voltage
        await c.call("scope_set_channel", channel="CH1", offset_raw=1030)
        set_dc(srv, 0.4)
        m = await c.call("scope_measure", items=["mean"], capture=True)
        assert m["channels"]["CH1"]["mean"] == pytest.approx(0.4, abs=0.02)  # ~1 LSB at 17 mV/LSB
        cfg = await c.call("scope_get_config")
        assert cfg["channels"][0]["scaling"] == "unit-calibration" and cfg["channels"][1]["scaling"] == "firmware"
        path = r["calibration_file"]
    assert path.endswith("1013D-SIM.json")
    async with client_for(srv, tmp_path) as c:  # a new server loads the file from <data_dir>/cal/<serial>.json
        m = await c.call("scope_measure", items=["mean"], capture=True)
        assert m["channels"]["CH1"]["mean"] == pytest.approx(0.4, abs=0.02)


def test_trigger_level_and_trace_use_the_calibration(sim_factory, tmp_path):
    srv = bad_unit(sim_factory, 0.3)
    with Scope.connect(srv.url) as s:
        s.set_channel(0, range_index=3)
        cal = UnitCalibration()
        for v in (0.0, 0.3):
            set_dc(srv, v)
            cap = s.capture(["CH1"], 1.0, keep_raw=True)
            d = cap.channels["CH1"]
            cfg = s.get_channel(0)
            cal.add_point(channel=0, range_index=3, offset_pwm=cfg.offset_pwm, volts=v, meanA=d.raw_a.mean(),
                          meanB=d.raw_b.mean(), fw_volts_per_lsb=cfg.volts_per_lsb, dc_shift_value=385)
        cal.refit()
        s.unit_cal = cal
        sc = s.scale(0)
        assert sc.volts_for_raw_a(sc.raw_a_for_volts(0.25)) == pytest.approx(0.25)
        tr = s.read_trace_capture(["CH1"])  # processed trace, compensated codes, per-core by sample parity
        assert float(np.mean(tr.channels["CH1"].volts)) == pytest.approx(0.3, abs=0.01)


def test_channel_model_fills_slope_and_unmeasured_ranges():
    """T8 offset sweep: the slope and the 0 V code at dcoffset 0 are the same on every range (offset injected
    after the attenuator), so a range measured at one offset borrows the slope and an unmeasured range uses Z."""
    cal = UnitCalibration()
    kw = dict(channel=0, fw_volts_per_lsb=0.0345, dc_shift_value=385)
    for dco in (0, 44, -44):  # range 2: 0 V at three offsets, centre 864
        cal.add_point(range_index=2, offset_pwm=864 + dco, volts=0.0, meanA=132.5 - 0.265 * dco,
                      meanB=127.0 - 0.265 * dco, center_cal=864, **kw)
    cal.add_point(range_index=4, offset_pwm=908, volts=0.0, meanA=132.5 - 0.265 * 44, meanB=127.0 - 0.265 * 44,
                  center_cal=864, **kw)  # range 4: one offset only
    cal.refit()
    assert cal.core_fit(0, 4, "A").slope == pytest.approx(-0.265, abs=1e-6)
    cm = cal.channel_fit[0]
    assert cm["A"]["Z"] == pytest.approx(132.5, abs=1e-3) and cm["B"]["Z"] == pytest.approx(127.0, abs=1e-3)

    class CalR(Cal):
        dc_calibration_offset = (860, 856, 864, 859, 864, 859, 859)
    sc = channel_scale(0, Cfg(5, 859 + 100, 882759), CalR(), cal)  # range 5 never measured, dcoffset +100
    assert sc.source.startswith("unit-calibration (channel model")
    assert sc.zero_a == pytest.approx(132.5 - 26.5, abs=1e-3) and sc.zero_b == pytest.approx(127.0 - 26.5, abs=1e-3)
    assert sc.gain_a == pytest.approx(882759 / 256e6)
