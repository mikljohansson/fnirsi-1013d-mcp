"""EXT trigger on the calibration pin (FPGA 0x1533, caps EXT_TRIGGER; wire values pending fw-gen2 confirmation)."""

from __future__ import annotations

import numpy as np
import pytest

from fnirsi1013d import analysis as A
from fnirsi1013d import protocol as P
from fnirsi1013d.scope import Scope
from fnirsi1013d.simulator import parse_signal

from test_server import client_for

GATE = "gate:period=200us,width=50us,high=3.3"


@pytest.fixture
def anyio_backend():
    return "asyncio"


def ext_sim(sim_factory, **kw):
    srv = sim_factory(variant="al3x", signal=GATE, **kw)
    srv.device.cfg.ext_signal = parse_signal(GATE)  # the same TX gate on CH1 and on the pin
    return srv


def test_caps_and_variant(sim_factory):
    with Scope.connect(ext_sim(sim_factory).url) as s:
        assert s.info.fpga_version == 0x1533 and s.profile().variant == "al3"
        assert s.info.caps & P.Caps.EXT_TRIGGER
    with Scope.connect(sim_factory().url) as s:
        assert not s.info.caps & P.Caps.EXT_TRIGGER
        with pytest.raises(Exception) as e:
            s.set_trigger(source=2)
        assert getattr(e.value, "status", None) == P.Status.E_UNSUPPORTED


def test_ext_trigger_point_lags_the_pin(sim_factory):
    """t = 0 is the EXT trigger; the pin edge (seen on CH1) is 20-25 ns earlier: 4-5 samples at 200 MS/s, plus
    up to 2 samples of trigger quantisation."""
    srv = ext_sim(sim_factory, noise_lsb=0.3)
    with Scope.connect(srv.url) as s:
        s.set_channel(0, range_index=1)
        s.set_timebase(divider=0)
        s.set_trigger(source=2, edge=0, mode=1, pretrigger_samples=200, total_samples=1000)
        offsets = []
        for _ in range(8):
            cap = s.capture(["CH1"], 1.0)
            assert cap is not None and cap.triggered
            v = cap.channels["CH1"].volts
            x = A.find_edge_near(v, 1.65, "rising", cap.pretrigger_samples, 12)
            assert x is not None
            offsets.append(x - cap.pretrigger_samples)
        assert -7.5 <= min(offsets) and max(offsets) <= -3.0, offsets


def test_legacy_ext_shift_puts_the_edge_at_the_pretrigger(sim_factory):
    """1.22 (fw 0.2.72): in the legacy layout the ADC pipeline delays the data 4 words behind the EXT trigger; the
    firmware writes that many fewer pretrigger words (2 at divider 0) and reports the shift in GET_CAPTURE_INFO 70
    and GET_DECIM 31, so the pin edge lands at the pretrigger as it does at 200 MS/s."""
    srv = ext_sim(sim_factory, noise_lsb=0.3)
    with Scope.connect(srv.url) as s:
        assert s.info.proto_minor >= 22
        s.set_channel(0, range_index=1)
        for div, shift in ((199, 8), (9, 8), (0, 4)):
            s.set_timebase(divider=div)
            s.set_trigger(source=2, edge=0, mode=1, pretrigger_samples=200, total_samples=1000)
            cap = s.capture(["CH1"], 1.0)
            assert cap is not None and cap.triggered
            assert s.capture_info().ext_shift == shift
            v = cap.channels["CH1"].volts
            x = A.find_edge_near(v, 1.65, "rising", cap.pretrigger_samples, 24)
            # the model's 22 ns EXT synchroniser is 4.4 samples at 200 MS/s, under a sample at the slower rates
            assert x is not None and abs(x - cap.pretrigger_samples) <= (5 if div == 0 else 2), (div, x)
        assert P.ext_shift_legacy(0) == 4 and P.ext_shift_legacy(1) == 8
        s.set_trigger(source=0)
        assert s.capture(["CH1"], 1.0) is not None and s.capture_info().ext_shift == 0


@pytest.mark.anyio
async def test_ext_and_generator_are_exclusive(sim_factory, tmp_path):
    async with client_for(ext_sim(sim_factory), tmp_path) as c:
        prof = await c.call("scope_profile")
        assert prof["live"]["ext_trigger"] is True and "0x1533" in prof["static"]["trigger"]
        r = await c.call("scope_set_trigger", source="EXT", edge="rising", level=1.0, mode="normal")
        assert r["applied"]["source"] == "EXT" and r["applied"]["level_volts"] is None
        assert any("3.3 V logic" in x for x in r["caveats"]) and any("level is ignored" in x for x in r["caveats"])
        assert "EXT trigger input" in await c.fails("scope_generator", enabled=True, frequency="1kHz",
                                                     duty_percent=50)
        await c.call("scope_set_trigger", source="CH1")
        tok = (await c.call("scope_generator", enabled=True, frequency="1kHz", duty_percent=50))["token"]
        await c.call("scope_generator", enabled=True, frequency="1kHz", duty_percent=50, confirm_token=tok)
        assert (await c.call("scope_status"))["ext"]["generator_drives_pin"] is True
        r = await c.call("scope_set_trigger", source="EXT")  # fw 0.2.9: selecting EXT switches the generator off
        assert r["applied"]["source"] == "EXT" and any("generator was switched off" in x for x in r["caveats"])
        st = await c.call("scope_status")
        assert st["ext"]["generator_drives_pin"] is False and c.srv.device.settings.gen.enable == 0
        cap = await c.call("scope_capture", channels=["CH1"])
        assert cap["trigger"] == "fired" and cap["ext_edge_seen"] is True
        assert (await c.call("scope_status"))["ext"]["edge_seen_since_arm"] is True
    async with client_for(sim_factory(), tmp_path) as c:
        assert "0x1532" in await c.fails("scope_set_trigger", source="EXT")


def test_device_refuses_gen_while_ext(sim_factory):
    with Scope.connect(ext_sim(sim_factory).url) as s:
        s.set_trigger(source=2)
        with pytest.raises(Exception) as e:
            s.set_gen(enable=True, freq_hz=1000, duty=0.5)
        assert getattr(e.value, "status", None) == P.Status.E_STATE
        assert np.isfinite(s.get_trigger(cached=False).source)


@pytest.mark.anyio
async def test_status_warns_when_the_ui_turns_the_generator_on_under_ext(sim_factory, tmp_path):
    srv = ext_sim(sim_factory)
    async with client_for(srv, tmp_path) as c:
        await c.call("scope_set_trigger", source="EXT")
        assert "warning" not in (await c.call("scope_status"))["ext"]
        srv.device.settings.gen.enable = 1  # the UI's generator Power switch is not blocked by the firmware
        st = await c.call("scope_status")
        assert st["ext"]["generator_drives_pin"] and "own generator" in st["ext"]["warning"]
