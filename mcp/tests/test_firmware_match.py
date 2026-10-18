"""Simulator behaviour that must match firmware v0.1.0 (docs/protocol.md section 14, host-visible items)."""

from __future__ import annotations

import socket
import struct
import time

import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d.scope import Scope
from fnirsi1013d.transport import DeviceError

from test_server import ToolFailed, client_for  # noqa: F401

FW010_AL3_CAPS = 0x1803  # GET_INFO caps reported by firmware 0.1.0 on the owner's AL3 unit


def test_pattern3_is_xorshift_words_little_endian():
    def ref(n: int) -> bytes:
        x, out = n, b""
        while len(out) < n:
            x ^= (x << 13) & 0xFFFFFFFF
            x ^= x >> 17
            x ^= (x << 5) & 0xFFFFFFFF
            out += struct.pack("<I", x)
        return out[:n]
    for n in (0, 1, 4, 7, 4096):
        assert P.test_pattern_bytes(n, 3) == ref(n)
    assert P.test_pattern_bytes(0, 3) == b""


def test_arm_while_armed_is_e_state(sim_factory):
    srv = sim_factory(signal="zero")
    with Scope.connect(srv.url) as s:
        s.ensure_control(watchdog_s=0)
        s.set_trigger(mode=1, level=250)
        s.arm()
        with pytest.raises(DeviceError) as e:
            s.arm()
        assert e.value.status == P.Status.E_STATE
        s.stop()
        s.arm()  # re-arm from ABORTED is fine
        s.stop()


def test_local_fpga_flags_are_zero(sim_factory):
    srv = sim_factory(legacy=True)
    with Scope.connect(srv.url) as s:
        s.read_trace(["CH1"])  # a UI capture completed
        st = s.status()
        assert st.control == P.Control.LOCAL
        assert st.flags & (P.StatusFlags.FPGA_TRIGGERED | P.StatusFlags.FPGA_DONE | P.StatusFlags.AUTO_TIMEOUT) == 0


def test_error_responses_are_not_replayed(legacy_sim):
    """14.5: only OK responses are cached; a retry after an error runs again."""
    s = socket.create_connection((legacy_sim.host, legacy_sim.port))
    try:
        p = P.FrameParser()

        def xfer(f: P.Frame) -> P.Frame:
            s.sendall(f.encode())
            s.settimeout(1)
            while True:
                ev = [e for e in p.feed(s.recv(4096)) if isinstance(e, P.Frame)]
                if ev:
                    return ev[0]
        arm = P.Frame(P.Cmd.ARM, 9, b"\0" * 4)
        assert xfer(arm).status == P.Status.E_NOT_REMOTE
        xfer(P.Frame(P.Cmd.SET_REMOTE, 10, P.SetRemoteReq(1, 0, 0).pack()))
        assert xfer(arm).status == P.Status.OK  # same seq and cmd, executed this time
    finally:
        s.close()


def test_keep_settings_flag_of_release_request(sim_factory):
    srv = sim_factory(legacy=True)
    with Scope.connect(srv.url) as s:
        div0 = s.get_timebase(cached=False).divider
        s.legacy_take_remote(keep_settings=True)  # acquire-time flag ...
        s.set_timebase(divider=3)
        s.legacy_release(keep_settings=False)  # ... but the release request's flag decides
        assert s.get_timebase(cached=False).divider == div0
        s.legacy_take_remote(keep_settings=True)
        s.set_timebase(divider=3)
        srv.device.user_tap()  # other exits use the acquire-time flag
        assert s.get_timebase(cached=False).divider == 3


def test_stock_gen_and_min_total(sim_factory):
    with Scope.connect(sim_factory(variant="stock").url) as s:
        g = s.get_gen()
        assert (g.enable, g.phase_inc, g.freq_mhz_actual, g.duty_bp_actual) == (1, 0, 1_000_000, 5000)
        assert s.info.min_total_samples == s.info.max_total_samples == 3000


def test_ascii_counts_for_watchdog(legacy_transport):
    transport = legacy_transport
    transport.request(P.Cmd.SET_REMOTE, P.SetRemoteReq(1, 0, 1).pack())
    for _ in range(4):
        time.sleep(0.4)
        assert transport.ascii("#VER").startswith("#OK")
    st = P.StatusResp.unpack(transport.request(P.Cmd.GET_STATUS).payload)
    assert st.control == P.Control.REMOTE


@pytest.mark.anyio
@pytest.mark.parametrize("anyio_backend", ["asyncio"])
async def test_server_with_firmware_010_caps(sim_factory, tmp_path, anyio_backend):
    """The capability set the real firmware reports today: no ACQUIRE_AVG, READ_SCREEN, FORCE, DEBUG_FPGA."""
    srv = sim_factory(signal="decay:tau=80us,amp=0.3", caps_override=FW010_AL3_CAPS)
    async with client_for(srv, tmp_path) as c:
        prof = await c.call("scope_profile")
        assert prof["live"]["device_averaging"] is False and any("T7" in u for u in prof["unverified"])
        r = await c.call("scope_averaged_capture", n=4, reject_clipped=False, on_device=True)
        assert r["averaging"] == "host" and any("no ACQUIRE_AVG" in x for x in r["caveats"])
        assert "SCREENSHOT" in await c.fails("scope_screenshot")
        await c.call("scope_acquisition_control", action="arm")
        assert "not verified" in await c.fails("scope_acquisition_control", action="force")
        await c.call("scope_acquisition_control", action="stop")
        assert "DEBUG_RAW" in await c.fails("scope_raw_fpga", ops=[{"cmd": 6}, {"read": 2}])
        g = await c.call("scope_generator", enabled=True, frequency="10kHz", duty_percent=50)
        assert g["confirmation_required"]  # GENERATOR_DDS is set (0x1803)
