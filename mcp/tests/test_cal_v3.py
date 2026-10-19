"""Protocol 1.12 (fw 0.2.45): calibration data v3, per-core and per-clock-class gain, GET_CAL 256 bytes (docs/protocol.md
section 14 row 51)."""

from __future__ import annotations

import json
import struct
from pathlib import Path

import anyio
import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d.calibration import UnitCalibration, channel_scale
from fnirsi1013d.scope import Scope

VECTORS = Path(__file__).resolve().parents[2] / "docs" / "protocol-vectors.json"
#: like the owner's unit: cores several codes apart, the gated clock moves the zeros, and the cores differ in gain
#: at full rate (bench 2026-10-18 CH1: A/B 1.033 full, 1.004 gated)
UNIT = dict(zero_error=((-3.0, -5.0), (1.0, 2.0)), gated_zero_shift=((-0.8, -3.6), (0.4, -1.0)),
            core_gain=(((1.020, 0.988), (1.002, 0.998)), ((0.995, 1.010), (1.0, 1.0))), noise_lsb=0.8)


def _flat(x):
    return tuple(v for a in x for v in (_flat(a) if isinstance(a, list) else (a,)))


def test_block_matches_the_firmware_vectors():
    d = json.loads(VECTORS.read_text(encoding="utf-8"))["adc_cal_v3"]
    block = bytes.fromhex(d["block_hex"])
    chans = P.parse_adc_cal_block(block)
    for got, want in zip(chans, d["channels"]):
        for k in ("pwm", "zero", "slope", "noise", "gain"):
            assert getattr(got, k) == _flat(want[k]), k
        assert (got.gain_range, got.gain_flags, got.gain_lever) == (want["gain_range"], want["gain_flags"],
                                                                     want["gain_lever"])
    with pytest.raises(ValueError, match="CRC"):
        P.parse_adc_cal_block(block[:20] + bytes([block[20] ^ 1]) + block[21:])
    # GET_CAL bytes 112..255 carry the same per-channel layout
    ch = chans[0]
    r = P.CalResp(pwm=ch.pwm, zero=ch.zero, slope=ch.slope, noise=ch.noise, gain=ch.gain, gain_range=ch.gain_range,
                  gain_flags=ch.gain_flags, gain_lever=ch.gain_lever, cal_version=3, cal_flags=0x0B)
    raw = r.pack()
    assert len(raw) >= 256 and raw[112:256] == block[12: 12 + 144]
    assert r.gain_corr(0, 1) == 2 * 33678 / 65536 and r.gain_corr(1, 0) == 2 * 32699 / 65536
    assert r.slope_codes(0, 0, 3) == -17257 / 65536 and r.slope_codes(1, 1, 3) == -17026 / 65536
    assert r.zero16_at(1, 0, 2, -10) == 2094 + round(17308 * 10 / 4096)
    a, b = 2 * 32607, 2 * 33678
    assert r.gain16_full() == (2 * a * b + (a + b) // 2) // (a + b)


def test_get_cal_256_layout():
    r = P.CalResp(cal_version=3, cal_flags=0x1F, comp_a=-3, comp_b=4, frac_a=5, frac_b=15, cal_zero_q8=129 * 256,
                  corr16=-40, cal_shift=-1, gain_a=66000, gain_b=64000, off_a=-123456, off_b=234567, gain16=65000)
    b = r.pack()
    assert struct.unpack_from("<BBhhBBiiiIIiiI", b, 72) == (3, 0x1F, -3, 4, 5, 15, 129 * 256, -40, -1, 66000, 64000,
                                                             -123456, 234567, 65000)
    old = P.CalResp.unpack(b[:72])
    assert old.cal_version == 0 and not old.v3
    with pytest.raises(ValueError, match="calibration v2"):  # fw 0.2.43-0.2.44: no longer decoded
        channel_scale(0, P.ChannelConfig(), P.CalResp(cal_version=2), None)


@pytest.mark.parametrize("divider, filt", [(19, "off"), (0, "off"), (19, "cic2")])
def test_v3_volts_on_both_cores_and_classes(sim_factory, divider, filt):
    """0.25 V reads 0.25 V through READ_RAW (host model), READ_TRACE (corrected on the scope) and the 16-bit path,
    on both cores and in both clock classes; the same unit without v3 is several codes off."""
    for v3 in (False, True):
        srv = sim_factory(variant="al3d", signal="dc:v=0.25", cal_v3=v3, **UNIT)
        with Scope.connect(srv.url) as s:
            s.set_trigger(total_samples=4000, mode=0)
            s.set_timebase(divider=divider)
            s.set_decim(filt)
            cal = s.get_cal(0)
            assert cal.cal_version == (3 if v3 else 1)
            assert bool(cal.cal_flags & P.CAL_GATED) == (divider > 0 and filt == "off")
            cap = s.capture(["CH1"])
            c = cap.channels["CH1"]
            lsb = c.volts_per_lsb
            tr = s.read_trace_capture(["CH1"]).channels["CH1"]
            if not v3:
                assert abs(c.volts.mean() - 0.25) / lsb > 2
                continue
            assert cal.cal_flags & P.CAL_GAINS_MEASURED and c.scale.source.startswith("scope calibration v3")
            assert abs(c.volts.mean() - 0.25) / lsb < 0.1 and abs(tr.volts.mean() - 0.25) / lsb < 0.1
            if filt == "off":  # the cores agree in level and gain: no A/B step at fs / 2
                assert abs(c.volts[0::2].mean() - c.volts[1::2].mean()) / lsb < 0.15
                _hdr, data = s.read_trace(["CH1"])
                code = data[0][1].astype(float)
                assert abs(code[0::2].mean() - code[1::2].mean()) < 0.15
            else:
                assert c.scale.wide_gain == pytest.approx(cal.gain16 / 65536, abs=2e-5)


def test_get_cal_current_correction_matches_the_host_model(sim_factory):
    """GET_CAL's next-readout gain / offset (Q16) are what the host's ChannelScale reproduces for the live class."""
    srv = sim_factory(variant="al3d", signal="dc:v=0", cal_v3=True, **UNIT)
    with Scope.connect(srv.url) as s:
        s.set_timebase(divider=19)
        cal = s.get_cal(0, cached=False)
        sc = s.scale(0)
        for core, g, off in (("A", cal.gain_a, cal.off_a), ("B", cal.gain_b, cal.off_b)):
            assert sc.codes_from_raw(100.0, core) == pytest.approx((100 * g + off) / 65536, abs=1 / 64)
        assert cal.cal_zero_q8 == 256 * P.cal_target(0, 1000)


def test_unit_file_contributes_gated_gains_only(sim_factory):
    """With v3 a unit file's zeros (here 5 codes wrong) are ignored; its gated gains are carried to the class."""
    srv = sim_factory(variant="al3d", signal="dc:v=0.25", cal_v3=True, **UNIT)
    with Scope.connect(srv.url) as s:
        cfg = s.get_channel(0)
        cal = s.get_cal(0)
        unit = UnitCalibration(unit="sim")
        vpl = cfg.volts_per_lsb
        for v in (0.0, 0.5):
            for pwm in (cfg.offset_pwm, cfg.offset_pwm + 100):
                m = cfg.zero_code + 5 + v / vpl
                unit.add_point(channel=0, range_index=cfg.range, offset_pwm=pwm, volts=v, meanA=m, meanB=m,
                               fw_volts_per_lsb=vpl, dc_shift_value=1000, source="test")
        unit.refit()
        s.unit_cal = unit
        sc = s.scale(0, cfg, P.CAL_CLASS_FULL)
        assert "unit-calibration gains" in sc.source
        assert sc.gain_a == pytest.approx(vpl * cal.gain_corr(0, 0) / cal.gain_corr(1, 0), rel=1e-3)
        s.set_trigger(total_samples=4000, mode=0)
        s.set_timebase(divider=19)
        c = s.capture(["CH1"]).channels["CH1"]
        assert abs(c.volts.mean() - 0.25) / vpl < 0.15


def test_calibration_info_shows_gains(sim_factory, tmp_path):
    from test_server import client_for
    srv = sim_factory(variant="al3d", cal_v3=True, **UNIT)

    async def main():
        async with client_for(srv, tmp_path) as c:
            r = await c.call("scope_calibration_info")
            sc = r["scope_calibration"]["CH1"]
            assert sc["version"] == 3 and sc["in_use"] == "v3"
            assert sc["flags"] == ["block_loaded", "bound_to_pwm_centres", "current_divider_gated", "gains_measured"]
            st = sc["stored"]
            g = st["gain_correction"]
            assert g["full"]["A"] == pytest.approx(1.0 / 1.020, abs=1e-4)
            assert g["gated"]["B"] == pytest.approx(1.0 / 0.998, abs=1e-4)
            assert st["gains_measured_on_range"] == 2 and st["gain_lever_codes"] == 1416 / 16
            assert st["zero_codes"]["gated"]["B"][0] - st["zero_codes"]["full"]["B"][0] == pytest.approx(-3.6, abs=0.07)
            assert set(st["slope_codes_per_count"]) == {"full", "gated"}
            assert sc["current"]["gain"]["A"] == pytest.approx(1.0 / 1.002, abs=1e-4)
            assert r["live"]["CH1"]["scaling"].startswith("scope calibration v3 (gated clock)")
            assert "calibration v3 sets the 0 V codes" in r["caveats"][0]
        async with client_for(sim_factory(), tmp_path) as c:
            sc = (await c.call("scope_calibration_info"))["scope_calibration"]["CH1"]
            assert sc["in_use"] == "1.00o5 compensations" and "stored" not in sc
    anyio.run(main)
