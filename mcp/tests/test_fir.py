"""Protocol 1.11 (fw 0.2.44, FPGA 0x1535): the CIC compensation FIR (row 49), USB suspend event fields (row 50) and
the soak tool's reopen mode."""

from __future__ import annotations

import math

import anyio
import numpy as np
import pytest

from fnirsi1013d import analysis as A
from fnirsi1013d import protocol as P
from fnirsi1013d.scope import Scope
from fnirsi1013d.simulator import parse_signal
from fnirsi1013d.transport import DeviceError

from test_soak import run_soak


def test_filter_names_and_status_decoding():
    assert P.parse_decim_filter("CIC2+FIR") == (2, True) and P.parse_decim_filter("off") == (0, False)
    with pytest.raises(ValueError):
        P.parse_decim_filter("cic4")
    c = P.decim_config(2, P.DECIM_SET_FIR, 19, 3000, P.DECIM_HAS_FIR)  # N = 20
    assert c.fir and c.fir_en and c.gain == 10 ** 2 and c.shift == P.decim_shift(100) + 1
    assert c.status == 0x80 | 0x40 | 0x20 | 2 and c.filter_name == "cic2+fir"
    assert P.DecimConfig.from_status(c.status, c.shift, 20) == c
    off = P.decim_config(2, P.DECIM_SET_FIR, 21, 3000, P.DECIM_HAS_FIR)  # N = 22: not a multiple of 4
    assert off.fir_en and not off.fir and off.gain == 22 ** 2 and off.filter_name == "cic2"
    assert not P.decim_config(2, P.DECIM_SET_FIR, 19, 3000, P.DECIM_HAS_CIC).fir_en  # 0x1534 never gets 0x56
    st = P.DecimState(filter=2, flags=P.SetDecimReq.FIR | P.SetDecimReq.NEG_A)
    assert st.filter_setting == "cic2+fir"
    assert P.SetDecimReq(2, P.SetDecimReq.FIR).pack() == bytes([2, 8, 0, 0])


@pytest.mark.parametrize("ev, text", [
    ((1, 0, 0), "bus_reset"),
    ((1, 1, 4), "bus_reset after suspend 4 ms"),
    ((4, 1, 0xFFFF), "disconnect after suspend >= 65535 ms"),
    ((2, 0x60, 0x0ABC), "suspend POWER 0x60 frame 700"),
    ((2, 0, 0), "suspend"),
    ((3, 0x60, 2500), "resume after 2500 ms suspended, RESUME interrupt, POWER 0x60"),
    ((3, 0xE0, 40), "resume after 40 ms suspended, inferred from SOFs (no RESUME interrupt), POWER 0x60"),
    # fw 0.2.49: a bits 6-5 = cause (0 SOFs, 1 EP0 setup, 2 EP2 OUT), bits 4-0 = POWER bits 4-0
    ((3, 0x90, 12), "resume after 12 ms suspended, inferred from SOFs (no RESUME interrupt), POWER bits 0x10"),
    ((3, 0xA0, 7), "resume after 7 ms suspended, inferred from an EP0 setup (no RESUME interrupt), POWER bits 0x00"),
    ((3, 0xD2, 9), "resume after 9 ms suspended, inferred from an EP2 OUT packet (no RESUME interrupt), POWER bits 0x12"),
    ((20, 0x42, 900), "usb_soft_disconnect (long suspend) after 900 s suspended, 4 suspends, back-off level 0"),
    ((20, 0x35, 10), "usb_soft_disconnect (retry storm) after 10 s suspended, 3 suspends, back-off level 1"),
    ((20, 0x02, 0xFFFF), "usb_soft_disconnect (long suspend) after >= 65535 s suspended, 0 suspends, back-off level 0"),
])
def test_suspend_event_fields(ev, text):
    assert P.DeviceEvent(0, *ev).describe() == text


def test_fir_variant_set_decim(sim_factory):
    srv = sim_factory(signal="dc:v=0.1")
    with Scope.connect(srv.url) as s:
        assert s.info.fpga_version == P.FW_FPGA_VERSION and s.info.caps & P.Caps.FIR and s.info.caps & P.Caps.DECIM
        s.set_timebase(divider=19)
        s.set_decim("cic2", flags=P.SetDecimReq.NEG_B)
        st = s.set_decim("cic2+fir")
        assert st.flags == P.SetDecimReq.FIR | P.SetDecimReq.NEG_B and st.filter_setting == "cic2+fir"
        d = st.next
        assert d.fir and d.gain == 100 and d.filter_name == "cic2+fir" and st.next_mode & P.DECIM_FIR_ACT
        assert st.ext_shift == 0 and d.ext_shift == 6  # from the 0x29 status alone (no cap-stage flag in it)
        assert s.set_decim("cic2").next.fir_en is False  # the name clears the FIR bit, keeps the others
        assert s.get_decim().flags == P.SetDecimReq.NEG_B
        with pytest.raises(DeviceError) as e:
            s.t.request(P.Cmd.SET_DECIM, bytes([2, 16, 0, 0]))  # flags bits 4-7 are E_BAD_ARG detail 1
        assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, 1)
    with Scope.connect(sim_factory(caps_clear=int(P.Caps.FIR)).url) as s:  # a firmware without the cap
        with pytest.raises(ValueError, match="no FIR"):
            s.set_decim("cic2+fir")


def test_fir_capture_volts_noise_and_ext(sim_factory):
    srv = sim_factory(signal="dc:v=0.1", noise_lsb=1.0)
    with Scope.connect(srv.url) as s:
        s.set_timebase(divider=199)  # N = 200: FIR active, CIC on 100
        s.set_trigger(pretrigger_samples=1000, total_samples=4000, mode=0)
        s.set_decim("cic2+fir")
        cap = s.capture(["CH1"])
        c = cap.channels["CH1"]
        assert cap.decim.fir and cap.decim.gain == 100 ** 2 and cap.capture_info.decim_mode & P.DECIM_FIR_ACT
        assert c.volts.mean() == pytest.approx(0.1, abs=1.5 * c.volts_per_lsb)
        expect = math.sqrt(1 + 1 / 12) / math.sqrt(A.decim_noise_n(2, 200, True))
        assert np.std(c.codes) == pytest.approx(expect, rel=0.25)
        tc = s.read_trace_capture(["CH1"])
        assert tc.decim == cap.decim and np.array_equal(tc.channels["CH1"].samples16, c.samples16)
    srv = sim_factory(signal="gate:period=200us,width=50us,high=0.5", noise_lsb=0.3)
    srv.device.cfg.ext_signal = parse_signal("gate:period=200us,width=50us,high=3.3")
    with Scope.connect(srv.url) as s:
        s.set_channel(0, range_index=4)
        s.set_timebase(divider=19)
        s.set_trigger(source=2, edge=0, mode=1, pretrigger_samples=200, total_samples=1000)
        s.set_decim("cic2+fir")
        offs = []
        for _ in range(3):
            cap = s.capture(["CH1"], 1.0)
            assert cap.triggered and cap.capture_info.ext_shift == 7
            x = A.find_edge_near(cap.channels["CH1"].volts, 0.25, "rising", cap.pretrigger_samples, 10)
            offs.append(x - cap.pretrigger_samples)
        # 0x153A+: the shift rounds the 40 ns capture-stage latency up to a whole sample at N = 20 (see test_decim)
        assert max(abs(o) for o in offs) <= 1.5, offs


def test_set_decimation_tool_fir(sim_factory, tmp_path):
    from test_server import client_for
    srv = sim_factory()

    async def main():
        async with client_for(srv, tmp_path) as c:
            await c.call("scope_set_timebase", divider=19)
            r = await c.call("scope_set_decimation", filter="cic3+fir")
            a = r["applied"]
            assert (a["setting"], a["effective"], a["gain"]) == ("cic3+fir", "cic3+fir", 1000)
            assert a["noise_equivalent_n"] == pytest.approx(A.decim_noise_n(3, 20, True), rel=1e-6)
            await c.call("scope_set_timebase", divider=21)  # N = 22
            r = await c.call("scope_set_decimation", filter="cic3+fir")
            assert r["applied"]["effective"] == "cic3" and "FIR requested but not active" in r["caveats"][0]
        async with client_for(sim_factory(caps_clear=int(P.Caps.FIR)), tmp_path) as c:
            assert "no compensation FIR" in await c.fails("scope_set_decimation", filter="boxcar+fir")
    anyio.run(main)


def test_soak_reopen_mode(sim):
    sim.device.log_event(2, 0x60, 123)
    sim.device.log_event(1, 1, 4)
    rc, lines, s = run_soak(sim, mode="reopen", rounds=2, idle=0.01, duration=0.15)
    text = "\n".join(lines)
    assert rc == 0, text
    assert "2 rounds with 0.01 s closed between them" in text
    assert "round 2/2: port closed, idle 0.01 s" in text and "round 2/2: host USB selective suspend" in text
    assert text.count("counters 36 usb_suspends=") == 2
    assert "bus_reset after suspend 4 ms" in text and "suspend POWER 0x60 frame 123" in text
