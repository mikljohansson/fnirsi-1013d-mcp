"""Protocol 1.10 (fw 0.2.43): calibration data v2, GET_CAL 248 bytes (docs/protocol.md section 14 row 48)."""

from __future__ import annotations

import json
import struct
from pathlib import Path

import anyio
import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d.calibration import UnitCalibration
from fnirsi1013d.scope import Scope

VECTORS = Path(__file__).resolve().parents[2] / "docs" / "protocol-vectors.json"
#: like the owner's unit: cores several codes off, and the gated encode clock moves the zeros again
UNIT = dict(signal="dc:v=0", zero_error=((-3.0, -5.0), (1.0, 2.0)), gated_zero_shift=((-0.8, -3.6), (0.4, -1.0)),
            noise_lsb=0.8)


def _flat(x):
    return tuple(v for a in x for v in (_flat(a) if isinstance(a, list) else (a,)))


def test_block_matches_the_firmware_vectors():
    d = json.loads(VECTORS.read_text(encoding="utf-8"))["adc_cal"]
    block = bytes.fromhex(d["block_hex"])
    chans = P.parse_adc_cal_block(block)
    for got, want in zip(chans, d["channels"]):
        assert (got.pwm, got.zero, got.slope, got.noise) == tuple(_flat(want[k]) for k in ("pwm", "zero", "slope", "noise"))
    with pytest.raises(ValueError, match="CRC"):
        P.parse_adc_cal_block(block[:20] + bytes([block[20] ^ 1]) + block[21:])
    # GET_CAL bytes 92..247 carry the same per-channel layout
    ch = chans[1]
    r = P.CalResp(pwm=ch.pwm, zero=ch.zero, slope=ch.slope, noise=ch.noise, cal_version=2, cal_flags=3)
    raw = r.pack()
    assert len(raw) == 248 and raw[92:248] == block[12 + 156: 12 + 2 * 156]
    assert r.zero_raw(0, 1, 4) == -77 / 16 and r.slope_codes(0, 5) == -20000 / 65536 and r.noise_rms(0, 1, 2) == 47 / 16
    assert r.zero_at(1, 0, 3, -10) == pytest.approx(2094 / 16 + 10 * 17308 / 65536)


def test_get_cal_layout_and_old_firmware():
    r = P.CalResp(cal_version=2, cal_flags=7, comp_a=-3, comp_b=4, frac_a=5, frac_b=15, cal_zero_q8=129 * 256,
                  corr16=-40, cal_shift=-1)
    b = r.pack()
    assert struct.unpack_from("<BBhhBBiii", b, 72) == (2, 7, -3, 4, 5, 15, 129 * 256, -40, -1)
    old = P.CalResp.unpack(b[:72])
    assert old.cal_version == 0 and not old.v2
    assert [P.cal_target(d, 385) for d in (-50, -7, 0, 7, 50)] == [140, 129, 128, 127, 116]  # C truncation
    assert P.cal_clock_class(0, False) == P.cal_clock_class(19, True) == 0 and P.cal_clock_class(19, False) == 1


@pytest.mark.parametrize("divider, filt", [(19, "off"), (0, "off"), (19, "cic2")])
def test_v2_puts_0v_at_zero_on_both_cores(sim_factory, divider, filt):
    """READ_RAW (per-class stored zeros on the host), READ_TRACE (corrected on the scope) and the 16-bit path
    (corr16) all read 0 V within a fraction of a code; the same unit without v2 is several codes off."""
    for v2 in (False, True):
        srv = sim_factory(variant="al3d", cal_v2=v2, **UNIT)
        with Scope.connect(srv.url) as s:
            s.set_trigger(total_samples=4000, mode=0)
            s.set_timebase(divider=divider)
            s.set_decim(filt)
            cal = s.get_cal(0)
            assert cal.cal_version == (2 if v2 else 1)
            assert bool(cal.cal_flags & P.CAL_GATED) == (divider > 0 and filt == "off")
            cap = s.capture(["CH1"])
            c = cap.channels["CH1"]
            vpl = c.volts_per_lsb
            tr = s.read_trace_capture(["CH1"]).channels["CH1"]
            if not v2:
                assert abs(c.volts.mean() / vpl) > 2
                continue
            assert c.scale.source.startswith("scope calibration v2")
            assert abs(c.volts.mean() / vpl) < 0.1 and abs(tr.volts.mean() / vpl) < 0.1
            if filt == "off":  # both cores agree (the A/B spur at Nyquist goes away)
                assert abs(c.volts[0::2].mean() - c.volts[1::2].mean()) / vpl < 0.15
                hdr, data = s.read_trace(["CH1"])
                assert abs(data[0][1].astype(float).mean() - P.cal_target(0, 1000)) < 0.1  # trace codes at T
            else:
                assert c.codes.mean() == pytest.approx(P.cal_target(0, 1000), abs=0.1)
                assert c.scale.adc_a_comp + c.scale.adc_b_comp == pytest.approx(2 * cal.corr16 / 16, abs=0.07)


def test_unit_file_zeros_are_not_applied_on_top(sim_factory, tmp_path):
    """With v2 in use the unit file contributes gains only; its zeros (here deliberately 5 codes wrong) are ignored."""
    srv = sim_factory(variant="al3d", cal_v2=True, **UNIT)
    with Scope.connect(srv.url) as s:
        cfg = s.get_channel(0)
        unit = UnitCalibration(unit="sim")
        for v, ma, mb in ((0.0, cfg.zero_code + 5, cfg.zero_code + 5), (0.5, cfg.zero_code + 5 + 0.5 / cfg.volts_per_lsb,
                                                                       cfg.zero_code + 5 + 0.5 / cfg.volts_per_lsb)):
            for pwm in (cfg.offset_pwm, cfg.offset_pwm + 100):
                unit.add_point(channel=0, range_index=cfg.range, offset_pwm=pwm, volts=v, meanA=ma, meanB=mb,
                               fw_volts_per_lsb=cfg.volts_per_lsb, dc_shift_value=1000, source="test")
        unit.refit()
        assert unit.has(0, cfg.range)
        s.unit_cal = unit
        s.set_trigger(total_samples=4000, mode=0)
        cap = s.capture(["CH1"])
        c = cap.channels["CH1"]
        assert "unit-calibration gains" in c.scale.source
        assert abs(c.volts.mean() / c.volts_per_lsb) < 0.1


def test_calibration_info_shows_scope_calibration(sim_factory, tmp_path):
    from test_server import client_for
    srv = sim_factory(variant="al3d", cal_v2=True, **UNIT)

    async def main():
        async with client_for(srv, tmp_path) as c:
            r = await c.call("scope_calibration_info")
            sc = r["scope_calibration"]["CH1"]
            assert sc["version"] == 2 and sc["in_use"] == "v2"
            assert sc["flags"] == ["block_loaded", "bound_to_pwm_centres", "current_divider_gated"]
            z = sc["stored"]["zero_codes"]
            assert z["gated"]["B"][0] - z["full"]["B"][0] == pytest.approx(-3.6, abs=0.07)
            assert z["full"]["A"][0] == pytest.approx(128 - 3, abs=0.07)
            assert sc["stored"]["slope_codes_per_count"]["A"][0] == pytest.approx(-0.1, abs=1e-4)
            assert r["live"]["CH1"]["scaling"].startswith("scope calibration v2 (gated clock)")
            assert "calibration v2 sets the 0 V codes" in r["caveats"][0]
        async with client_for(sim_factory(), tmp_path) as c:
            r = await c.call("scope_calibration_info")
            assert r["scope_calibration"]["CH1"]["in_use"] == "1.00o5 compensations"
            assert "stored" not in r["scope_calibration"]["CH1"]
    anyio.run(main)
