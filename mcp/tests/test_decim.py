"""Protocol 1.9 (fw 0.2.30, FPGA 0x1534): the decimation filter, 16-bit samples (docs/protocol.md section 14 row 47)."""

from __future__ import annotations

import json
import math
import struct
from pathlib import Path

import anyio
import numpy as np
import pytest

from fnirsi1013d import analysis as A
from fnirsi1013d import protocol as P
from fnirsi1013d.scope import Scope
from fnirsi1013d.simulator import parse_signal
from fnirsi1013d.transport import DeviceError

VECTORS = Path(__file__).resolve().parents[2] / "docs" / "protocol-vectors.json"
DC = "dc:v=0.1"  # 0.1 V on range 5: about 28 codes above mid-scale
GATE = "gate:period=200us,width=50us,high=0.5"


def dsim(sim_factory, signal=DC, **kw):
    return sim_factory(signal=signal, **kw)


# --- maths and wire format ----------------------------------------------------------------------------------


def test_decim_maths_match_the_firmware_vectors():
    """docs/protocol-vectors.json "decim" is generated from firmware/scope/pc_decim.c."""
    d = json.loads(VECTORS.read_text(encoding="utf-8"))["decim"]
    assert len(d["cases"]) >= 80
    assert sum(1 for c in d["cases"] if c.get("fir_req")) >= 80  # 1.11 FIR cases
    for c in d["cases"]:
        fl = P.DECIM_SET_FIR if c.get("fir_req") else 0
        x = P.decim_config(c["k_req"], fl, c["divider"], c["total"], P.DECIM_HAS_FIR)
        assert (x.k, x.n, x.gain, x.shift, int(x.fir)) == (c["k"], c["n"], c["gain"], c["shift"], c.get("fir", 0)), c
        assert (x.rate(c["divider"]), x.settle_us, x.ext_shift) == (c["rate"], c["settle_us"], c["ext_shift"]), c
        assert [P.decim_level16(v, x.gain, x.shift) for v in d["levels"]] == c["level16"] if x.k else not c["level16"]


def test_decim_config_fallbacks():
    assert P.decim_config(3, 0, 10333, 3000).k == 3  # N = 10334
    c = P.decim_config(3, 0, 10335, 3000)
    assert (c.k, c.why, c.n) == (2, 4, 10336)
    assert P.decim_config(2, 0, 2_000_000, 3000).why == 5
    assert P.decim_config(1, 0, 0, 3000).why == 2 and P.decim_config(1, 0, 9, 8192).why == 3
    assert P.decim_config(1, P.DECIM_SWAP, 9, 3000, P.DECIM_HAS_NONE).why == 6
    c = P.decim_config(2, P.DECIM_NEG_A, 19, 3000)
    assert c.mode == 0x0A and c.status == 0x8A and c.code_per_count == pytest.approx(2 / 400)
    assert P.decim_to_code(65535, P.decim_scale_q24(400, 1), 0) == 255
    assert P.decim_to_code(25600, P.decim_scale_q24(400, 1), 24) == 129  # 128 + 1.5 with the Q24 scale just below 1/200


def test_wire_layouts():
    assert P.SetDecimReq(3, 5).pack() == bytes([3, 5, 0, 0]) and P.DecimState.size() == 36
    st = P.DecimState(2, 1, 0x86, 1, 20, 400, 19, 10_000_000, 0xFF, 0, 0, 0, 8190, 0, 2)
    b = st.pack()
    assert struct.unpack_from("<BBBBIQII", b) == (2, 1, 0x86, 1, 20, 400, 19, 10_000_000)
    assert b[24] == 0xFF and struct.unpack_from("<HBB", b, 28) == (8190, 0, 2)
    assert st.next == P.DecimConfig(2, P.DECIM_SWAP, 1, 0, 20, 400)
    h = P.CaptureInfoHeader(7, 1, 0, 1, 100, 50, 19, 24, 0, 0, 128, 0, 1000, 4000)
    old = P.CaptureInfo(h, [P.CaptureChannelInfo(), P.CaptureChannelInfo()])
    assert len(old.pack()) == 68 and P.CaptureInfo.unpack(old.pack()).decim is None
    new = P.CaptureInfo(h, old.channels, 0x82, 1, 2, 20, has_decim=True)
    raw = new.pack()
    assert len(raw) == 76 and raw[68:] == bytes([0x82, 1, 2, 0, 20, 0, 0, 0])
    back = P.CaptureInfo.unpack(raw)
    assert back.decim == P.DecimConfig(2, 0, 1, 0, 20, 400)
    assert back.trigger().total_words == 4000 and back.timebase().dt == pytest.approx(100e-9)
    assert P.ReadTraceHeader(decim_mode=0x83, decim_shift=15).pack()[30:] == bytes([0x83, 15])
    assert P.Timebase(divider=198, sample_period_ps=1_000_000).dt == pytest.approx(1e-6)  # N = 200, not 199


# --- simulator + client -------------------------------------------------------------------------------------


def test_get_set_decim_and_settings(sim_factory):
    srv = dsim(sim_factory)
    with Scope.connect(srv.url) as s:
        assert s.info.caps & P.Caps.DECIM and s.info.fpga_version == 0x153B
        st = s.get_decim()
        assert st.filter == 0 and st.next.k == 0 and st.last_mode == 0xFF and st.why == 1
        seq = s.status().settings_seq
        s.set_timebase(divider=198)
        st = s.set_decim("cic2")
        assert s.status().settings_seq != seq
        assert (st.filter, st.next.k, st.next.n, st.next.shift, st.rate_hz) == (2, 2, 200, 8, 1_000_000)
        tb = s.get_timebase(cached=False)
        assert tb.divider == 198 and tb.dt == pytest.approx(1e-6) and tb.sample_rate_hz == 1_000_000
        tr = s.get_trigger(cached=False)
        assert tr.total_samples == 3000 and tr.total_words == 3000 and tr.pretrigger_words == 1500
        for req, detail in ((bytes([4, 0, 0, 0]), 0), (bytes([1, 16, 0, 0]), 1)):  # filter 4; flags bits 4-7
            with pytest.raises(DeviceError) as e:
                s.t.request(P.Cmd.SET_DECIM, req)
            assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, detail)
        with pytest.raises(DeviceError) as e:
            s.set_trigger(total_samples=10000)
        assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, 5)
        s.set_decim("off")
        s.set_trigger(total_samples=10000)
        with pytest.raises(DeviceError) as e:
            s.set_decim("boxcar")
        assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, 0)
        s.set_trigger(total_samples=4000)
        s.set_decim(1, flags=P.SetDecimReq.NEG_A)
        assert s.set_decim("cic3").flags == P.SetDecimReq.NEG_A  # flags kept by default
        s.set_timebase(divider=19999)  # N = 20000: CIC3 not exact
        st = s.get_decim()
        assert (st.next.k, st.why, P.DECIM_WHY[st.why]) == (2, 4, "CIC3 not exact at this N: CIC2 used")
        s.set_timebase(divider=0)
        assert s.get_decim().why == 2
    with Scope.connect(sim_factory(caps_clear=int(P.Caps.DECIM)).url) as s:  # a firmware without the cap
        assert not s.info.caps & P.Caps.DECIM
        with pytest.raises(DeviceError) as e:
            s.get_decim()
        assert e.value.status == P.Status.E_UNSUPPORTED


@pytest.mark.parametrize("filt, k", [("boxcar", 1), ("cic2", 2), ("cic3", 3)])
def test_decimated_capture_volts_and_noise(sim_factory, filt, k):
    """16-bit samples convert to the same volts as legacy captures, with the noise reduced by about sqrt(N_eff)."""
    srv = dsim(sim_factory, noise_lsb=1.0)
    with Scope.connect(srv.url) as s:
        s.set_timebase(divider=199)  # 1 MSa/s, N = 200
        s.set_trigger(pretrigger_samples=1000, total_samples=4000, mode=0)
        legacy = s.capture(["CH1"])
        assert legacy.decim is None and legacy.channels["CH1"].samples16 is None
        st = s.set_decim(filt)
        assert st.next.k == k
        cap = s.capture(["CH1"])
        c = cap.channels["CH1"]
        assert cap.decim.k == k and cap.decim.n == 200 and cap.dt == pytest.approx(1e-6) and cap.n == 4000
        assert c.samples16.dtype == np.uint16 and c.samples16.size == 4000
        cal = s.get_cal(0)
        comp = (cal.adc_a_comp + cal.adc_b_comp) / 2  # the sim's core B offset is compensated by -3
        assert c.codes == pytest.approx(c.samples16 * cap.decim.code_per_count + comp, abs=1e-9)
        vpl = c.volts_per_lsb
        assert c.volts.mean() == pytest.approx(legacy.channels["CH1"].volts.mean(), abs=0.3 * vpl)
        assert c.volts.mean() == pytest.approx(0.1, abs=1.5 * vpl)
        expect = math.sqrt(1 + 1 / 12) / math.sqrt(A.decim_noise_n(k, 200))
        assert np.std(c.codes) == pytest.approx(expect, rel=0.25)
        assert np.std(legacy.channels["CH1"].codes) > 8 * np.std(c.codes)
        tc = s.read_trace_capture(["CH1"])  # READ_TRACE WIDE: the same capture's u16 samples
        assert tc.decim == cap.decim and tc.dt == pytest.approx(1e-6)
        assert np.array_equal(tc.channels["CH1"].samples16, c.samples16)
        hdr, data = s.read_trace(["CH1"])  # without WIDE: the firmware's 8-bit conversion
        assert hdr.decim_mode == cap.decim.status and hdr.decim_shift == cap.decim.shift
        conv = [P.decim_to_code(int(v), cap.decim.scale_q24, 8 * (cal.adc_a_comp + cal.adc_b_comp))
                for v in c.samples16[:50]]
        assert list(data[0][1][:50]) == conv


def test_decimated_trigger_and_ext_latency(sim_factory):
    """ADC trigger on the 16-bit samples: the crossing is at sample `pretrigger`. EXT with K >= 1: the firmware
    lowers the pretrigger words by the rounded latency, so the pin edge (seen on CH1) is at `pretrigger` too."""
    srv = dsim(sim_factory, signal=GATE, noise_lsb=0.3)
    srv.device.cfg.ext_signal = parse_signal("gate:period=200us,width=50us,high=3.3")
    with Scope.connect(srv.url) as s:
        s.set_channel(0, range_index=4)
        s.set_timebase(divider=19)  # 10 MSa/s, N = 20
        s.set_trigger(source=0, edge=0, level=150, mode=1, pretrigger_samples=200, total_samples=1000)
        s.set_decim("cic2")
        cap = s.capture(["CH1"], 1.0)
        assert cap.triggered and cap.decim.k == 2
        lvl = cap.channels["CH1"].codes
        assert lvl[199] < 150 <= lvl[200] + 0.5
        s.set_trigger(source=2, edge=0)
        st = s.get_decim()
        # 0x153A+ (the capture stages, 1.24): one sample more than the 0x1534 shift of 2 at N = 20
        assert st.ext_shift == P.decim_config(2, 0, 19, 1000, P.DECIM_HAS_FIR | P.DECIM_HAS_CAPSTAGES).ext_shift == 3
        assert P.decim_config(2, 0, 19, 1000).ext_shift == 2
        offs = []
        for _ in range(4):
            cap = s.capture(["CH1"], 1.0)
            assert cap.triggered and cap.capture_info.ext_shift == 3
            v = cap.channels["CH1"].volts
            x = A.find_edge_near(v, 0.25, "rising", cap.pretrigger_samples, 8)
            offs.append(x - cap.pretrigger_samples)
        # 0x153A+: the firmware's shift rounds the 40 ns capture-stage latency (0.4 sample at N = 20) up to a whole
        # sample, the model adds the 40 ns: the edge lands about a sample early (0x1534 model: within 0.75); the bench
        # on 0x153B decides which is right
        assert max(abs(o) for o in offs) <= 1.5, offs


# --- MCP tools ----------------------------------------------------------------------------------------------


def test_scope_set_decimation_tool(sim_factory, tmp_path):
    from test_server import client_for
    srv = dsim(sim_factory)

    async def main():
        async with client_for(srv, tmp_path) as c:
            await c.call("scope_set_timebase", divider=199)
            r = await c.call("scope_set_decimation", filter="cic2")
            a = r["applied"]
            assert (a["effective"], a["n"], a["shift"], a["output_rate"]) == ("cic2", 200, 8, 1_000_000)
            assert a["effective_bits_estimate"] == pytest.approx(8 + 0.5 * math.log2(A.decim_noise_n(2, 200)), abs=1e-3)
            assert any("Filter setting" in cv for cv in r["caveats"])
            cfg = await c.call("scope_get_config")
            assert cfg["decimation"]["effective"] == "cic2" and cfg["timebase"]["sample_rate"] == 1_000_000
            cap = await c.call("scope_capture", channels=["CH1"])
            assert cap["decimation"]["n"] == 200 and any("decimated capture (cic2" in cv for cv in cap["caveats"])
            with np.load(cap["file"]) as z:
                assert z["CH1_samples16"].dtype == np.uint16 and z["CH1_samples16"].size == cap["n_samples"]
            await c.call("scope_set_timebase", divider=29999)
            r = await c.call("scope_set_decimation", filter="cic3")
            assert r["applied"]["effective"] == "cic2" and "CIC3 not exact" in r["caveats"][0]
            await c.call("scope_set_decimation", filter="off")
            await c.call("scope_set_trigger", total_samples=10000)
            assert "total_samples <= 8190" in await c.fails("scope_set_decimation", filter="boxcar")
        async with client_for(sim_factory(caps_clear=int(P.Caps.DECIM)), tmp_path) as c:
            assert "no decimation filter" in await c.fails("scope_set_decimation", filter="boxcar")
            assert "decimation" not in await c.call("scope_get_config")
    anyio.run(main)
