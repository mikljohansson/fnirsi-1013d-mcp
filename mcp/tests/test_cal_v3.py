"""Protocol 1.12 (fw 0.2.45): calibration data v3, per-core gain, GET_CAL 256 bytes (docs/protocol.md section 14 row
51). Since fw 0.2.105 (FPGA 0x153D, the ADCs always at full rate) there is one calibration class: the block's class-1
(gated) slots are reserved and ignored."""

from __future__ import annotations

import json
import struct
import zlib
from pathlib import Path

import anyio
import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d.calibration import UnitCalibration, channel_scale
from fnirsi1013d.scope import Scope

VECTORS = Path(__file__).resolve().parents[2] / "docs" / "protocol-vectors.json"
#: like the owner's unit: cores several codes apart in zero and about 3 % apart in gain (bench 2026-10-18 CH1 at full
#: rate: A/B 1.033); the mean gain of each channel is 1 (what the volts calibration owns)
UNIT = dict(zero_error=((-3.0, -5.0), (1.0, 2.0)), core_gain=((1.016, 0.984), (0.995, 1.005)), noise_lsb=0.8)


def _flat(x):
    return tuple(v for a in x for v in (_flat(a) if isinstance(a, list) else (a,)))


def test_block_matches_the_firmware_vectors():
    d = json.loads(VECTORS.read_text(encoding="utf-8"))["adc_cal_v3"]
    block = bytes.fromhex(d["block_hex"])
    assert struct.unpack_from("<HHH", block, 4) == (3, 304, 1)  # fw 0.2.105 writes one class
    chans = P.parse_adc_cal_block(block)
    for got, want in zip(chans, d["channels"]):
        for k in ("pwm", "zero", "slope", "noise", "gain"):
            assert getattr(got, k) == _flat(want[k]), k
        assert (got.gain_range, got.gain_flags, got.gain_lever) == (want["gain_range"], want["gain_flags"],
                                                                     want["gain_lever"])
    for i in range(2):  # the class-1 slots are written as zero: zero, slope, noise, gain
        o = 12 + 144 * i
        for a, n in ((o + 36, 24), (o + 84, 24), (o + 120, 12), (o + 136, 4)):
            assert block[a: a + n] == bytes(n)
    with pytest.raises(ValueError, match="CRC"):
        P.parse_adc_cal_block(block[:20] + bytes([block[20] ^ 1]) + block[21:])
    # a block of fw 0.2.45-0.2.104 (classes 2, gated slots filled) loads with its class-0 values
    old = bytearray(block[:300])
    struct.pack_into("<H", old, 8, 2)
    for i in range(2):
        o = 12 + 144 * i
        for a, n in ((o + 36, 24), (o + 84, 24), (o + 120, 12), (o + 136, 4)):
            old[a: a + n] = bytes([0x5A]) * n
    old += struct.pack("<I", zlib.crc32(bytes(old)))
    assert P.parse_adc_cal_block(bytes(old)) == chans
    struct.pack_into("<H", old, 8, 3)
    old[300:] = struct.pack("<I", zlib.crc32(bytes(old[:300])))
    with pytest.raises(ValueError, match="classes 3"):
        P.parse_adc_cal_block(bytes(old))
    # GET_CAL bytes 112..255 carry the same per-channel layout
    ch, want = chans[0], d["channels"][0]
    r = P.CalResp(pwm=ch.pwm, zero=ch.zero, slope=ch.slope, noise=ch.noise, gain=ch.gain, gain_range=ch.gain_range,
                  gain_flags=ch.gain_flags, gain_lever=ch.gain_lever, cal_version=3, cal_flags=0x0B)
    raw = r.pack()
    assert len(raw) >= 256 and raw[112:256] == block[12: 12 + 144]
    assert P.CalResp.unpack(raw).zero == ch.zero
    assert r.gain_corr(1) == 2 * want["gain"][1] / 65536 and r.gain_corr(0) == 2 * want["gain"][0] / 65536
    assert r.slope_codes(0, 3) == want["slope"][0][3] / 65536 and r.slope_codes(1, 3) == want["slope"][1][3] / 65536
    assert r.zero16_at(0, 2, -10) == want["zero"][0][2] + P._rdiv(want["slope"][0][2] * -10, 4096)
    a, b = 2 * want["gain"][0], 2 * want["gain"][1]
    assert r.stored_gain16() == (2 * a * b + (a + b) // 2) // (a + b)


def test_get_cal_256_layout():
    r = P.CalResp(cal_version=3, cal_flags=0x1B, comp_a=-3, comp_b=4, frac_a=5, frac_b=15, cal_zero_q8=129 * 256,
                  corr16=-40, cal_shift=-1, gain_a=66000, gain_b=64000, off_a=-123456, off_b=234567, gain16=65000)
    b = r.pack()
    assert struct.unpack_from("<BBhhBBiiiIIiiI", b, 72) == (3, 0x1B, -3, 4, 5, 15, 129 * 256, -40, -1, 66000, 64000,
                                                             -123456, 234567, 65000)
    old = P.CalResp.unpack(b[:72])
    assert old.cal_version == 0 and not old.v3
    with pytest.raises(ValueError, match="calibration v2"):  # fw 0.2.43-0.2.44: no longer decoded
        channel_scale(0, P.ChannelConfig(), P.CalResp(cal_version=2), None)


@pytest.mark.parametrize("divider, filt", [(19, "off"), (0, "off"), (19, "cic2")])
def test_v3_volts_on_both_cores_at_every_rate(sim_factory, divider, filt):
    """0.25 V reads 0.25 V through READ_RAW (host model), READ_TRACE (corrected on the scope) and the 16-bit path,
    on both cores at 200 MSa/s and at a divided rate; the same unit without v3 is several codes off."""
    for v3 in (False, True):
        srv = sim_factory(signal="dc:v=0.25", cal_v3=v3, **UNIT)
        with Scope.connect(srv.url) as s:
            s.set_trigger(total_samples=4000, mode=0)
            s.set_timebase(divider=divider)
            s.set_decim(filt)
            cal = s.get_cal(0)
            assert cal.cal_version == (3 if v3 else 1)
            assert not cal.cal_flags & 0x04  # bit 2 (the gated class) is reserved since fw 0.2.105
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
    """GET_CAL's next-readout gain / offset (Q16) are what the host's ChannelScale reproduces, at any divider."""
    srv = sim_factory(signal="dc:v=0", cal_v3=True, **UNIT)
    with Scope.connect(srv.url) as s:
        for divider in (19, 0):
            s.set_timebase(divider=divider)
            cal = s.get_cal(0, cached=False)
            sc = s.scale(0)
            for core, g, off in (("A", cal.gain_a, cal.off_a), ("B", cal.gain_b, cal.off_b)):
                assert sc.codes_from_raw(100.0, core) == pytest.approx((100 * g + off) / 65536, abs=1 / 64)
            assert cal.cal_zero_q8 == 256 * P.cal_target(0, 1000)


def test_unit_file_contributes_gains_only(sim_factory):
    """With v3 a unit file's zeros (here 5 codes wrong) are ignored; its per-core gains replace the firmware's
    volts per corrected code."""
    srv = sim_factory(signal="dc:v=0.25", cal_v3=True, **UNIT)
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
        sc = s.scale(0, cfg)
        assert "unit-calibration gains" in sc.source
        assert sc.gain_a == pytest.approx(vpl * cal.gain_corr(0), rel=1e-3)
        s.set_trigger(total_samples=4000, mode=0)
        s.set_timebase(divider=19)
        c = s.capture(["CH1"]).channels["CH1"]
        assert abs(c.volts.mean() - 0.25) / vpl < 0.15


def test_calibration_info_shows_gains(sim_factory, tmp_path):
    from test_server import client_for
    srv = sim_factory(cal_v3=True, **UNIT)

    async def main():
        async with client_for(srv, tmp_path) as c:
            r = await c.call("scope_calibration_info")
            sc = r["scope_calibration"]["CH1"]
            assert sc["version"] == 3 and sc["in_use"] == "v3"
            assert sc["flags"] == ["block_loaded", "bound_to_pwm_centres", "gains_measured"]
            st = sc["stored"]
            g = st["gain_correction"]
            assert g["A"] == pytest.approx(1.0 / 1.016, abs=1e-4) and g["B"] == pytest.approx(1.0 / 0.984, abs=1e-4)
            assert st["gains_measured_on_range"] == 2 and st["gain_lever_codes"] == 1416 / 16
            # B - A: the zero error (-5 against -3) plus the model's CH1 core B offset (+3)
            assert st["zero_codes"]["B"][0] - st["zero_codes"]["A"][0] == pytest.approx(1.0, abs=0.07)
            assert set(st["slope_codes_per_count"]) == {"A", "B"}
            assert sc["current"]["gain"]["A"] == pytest.approx(1.0 / 1.016, abs=1e-4)
            assert r["live"]["CH1"]["scaling"].startswith("scope calibration v3")
            assert "calibration v3 sets the 0 V codes" in r["caveats"][0]
        async with client_for(sim_factory(), tmp_path) as c:
            sc = (await c.call("scope_calibration_info"))["scope_calibration"]["CH1"]
            assert sc["in_use"] == "1.00o5 compensations" and "stored" not in sc
    anyio.run(main)
