"""Every MCP tool end-to-end against the simulator, with the server running in-process."""

from __future__ import annotations

import json
import math
from contextlib import asynccontextmanager
from pathlib import Path

import numpy as np
import pytest
from mcp.shared.memory import create_connected_server_and_client_session

from fnirsi1013d.server import ServerConfig, build_server

pytestmark = pytest.mark.anyio

ALL_TOOLS = {
    "scope_info", "scope_profile", "scope_status", "scope_get_config", "scope_read_calibration", "scope_screenshot",
    "scope_remote_session", "scope_set_channel", "scope_set_timebase", "scope_set_trigger", "scope_capture",
    "scope_fetch_waveform", "scope_acquisition_control", "scope_averaged_capture", "scope_capture_and_fft",
    "scope_decay_fit", "scope_measure", "scope_generator", "scope_self_test_loopback", "scope_raw_fpga",
    "scope_enter_fel",
}


@pytest.fixture
def anyio_backend():
    return "asyncio"


class Client:
    def __init__(self, session, sess, srv):
        self.session = session
        self.sess = sess
        self.srv = srv

    async def call(self, name: str, **args):
        res = await self.session.call_tool(name, args)
        if res.isError:
            raise ToolFailed(res.content[0].text if res.content else "error")
        if res.structuredContent is not None:
            sc = res.structuredContent
            return sc.get("result", sc) if set(sc) == {"result"} else sc
        return res.content

    async def fails(self, name: str, **args) -> str:
        with pytest.raises(ToolFailed) as e:
            await self.call(name, **args)
        return str(e.value)


class ToolFailed(Exception):
    pass


@asynccontextmanager
async def client_for(srv, tmp_path: Path, **cfgkw):
    cfgkw.setdefault("max_tier", 4)
    cfgkw.setdefault("enable_raw", True)
    cfgkw.setdefault("keepalive_s", 3600)
    cfg = ServerConfig(port=srv.url, data_dir=tmp_path / "data", audit_log=tmp_path / "audit.jsonl", **cfgkw)
    mcp, sess = build_server(cfg)
    try:
        async with create_connected_server_and_client_session(mcp) as session:
            yield Client(session, sess, srv)
    finally:
        sess.close()


@pytest.fixture
async def client(sim_factory, tmp_path):
    srv = sim_factory(signal="sine:freq=10kHz,amp=0.25")
    async with client_for(srv, tmp_path) as c:
        yield c


async def setup_decay(c: Client, tau: str = "80us"):
    await c.call("scope_set_channel", channel="CH1", volts_per_div=0.1, coupling="DC")
    await c.call("scope_set_channel", channel="CH2", volts_per_div=2.5)
    await c.call("scope_set_timebase", sample_rate="10MS/s")
    await c.call("scope_set_trigger", source="CH2", edge="falling", level=1.65, mode="normal",
                 pretrigger_fraction=0.05, total_samples=16000)


# --- registry ---------------------------------------------------------------------------------------------


async def test_tool_list_and_annotations(client):
    tools = (await client.session.list_tools()).tools
    names = {t.name for t in tools}
    assert names == ALL_TOOLS
    by = {t.name: t for t in tools}
    assert by["scope_info"].annotations.readOnlyHint is True
    assert by["scope_generator"].annotations.destructiveHint is True
    assert "tap on the banner" in by["scope_remote_session"].description
    assert "Safety tier 3" in by["scope_generator"].description
    assert "50 mV/div is a software zoom" in by["scope_set_channel"].description


async def test_tiers_limit_registration(sim, tmp_path):
    async with client_for(sim, tmp_path, max_tier=1, enable_raw=False) as c:
        names = {t.name for t in (await c.session.list_tools()).tools}
    assert "scope_capture" in names and "scope_set_channel" not in names and "scope_generator" not in names
    assert "scope_raw_fpga" not in names
    async with client_for(sim, tmp_path, max_tier=4, enable_raw=False) as c:
        names = {t.name for t in (await c.session.list_tools()).tools}
    assert "scope_generator" in names and "scope_raw_fpga" not in names and "scope_enter_fel" not in names


# --- tier 0 -----------------------------------------------------------------------------------------------


async def test_info_profile_status_config_cal(client):
    info = await client.call("scope_info")
    assert info["fpga_variant"] == "al3" and info["fpga_version"] == "0x1532" and "GENERATOR_DDS" in info["caps"]
    prof = await client.call("scope_profile")
    assert "200 MS/s" in prof["static"]["max_sample_rate"] and prof["live"]["max_total_samples"] == 16382
    assert any("T3" in u for u in prof["unverified"])
    st = await client.call("scope_status")
    assert st["control"] == "LOCAL" and st["battery_level"] == 18
    cfg = await client.call("scope_get_config")
    assert cfg["channels"][0]["channel"] == "CH1" and cfg["timebase"]["divider"] == 19
    assert cfg["generator"]["available"] is True
    cal = await client.call("scope_read_calibration")
    assert cal["channels"][0]["adc_b_comp"] == -3
    assert abs(cal["channels"][0]["volts_per_lsb_1x_per_range"][5] - 3.624e-3) < 1e-5  # default 3.8e6


async def test_fetch_waveform_in_local_is_tier0(client):
    r = await client.call("scope_fetch_waveform", channels=["CH1"])
    assert r["ok"] and r["n_samples"] == 3000 and Path(r["file"]).exists()
    st = await client.call("scope_status")
    assert st["control"] == "LOCAL"  # did not take the scope


async def test_screenshot_png(client):
    res = await client.session.call_tool("scope_screenshot", {})
    assert not res.isError
    img = res.content[0]
    assert img.type == "image" and img.mimeType == "image/png"
    import base64
    data = base64.b64decode(img.data)
    assert data[:8] == b"\x89PNG\r\n\x1a\n"


# --- tier 1/2 configuration and capture -------------------------------------------------------------------


async def test_remote_session_acquire_release(client):
    r = await client.call("scope_remote_session", action="acquire")
    assert r["control"] == "REMOTE"
    r = await client.call("scope_remote_session", action="release")
    assert r["control"] == "LOCAL"


async def test_set_channel_snaps_and_reports(client):
    r = await client.call("scope_set_channel", channel="CH1", volts_per_div="9", probe=10, coupling="AC",
                          offset_div=1.0)
    a = r["applied"]
    assert a["volts_per_div"] == 10.0 and a["range_index"] == 2 and a["coupling"] == "AC" and a["probe"] == 10
    assert abs(a["offset_div"] - 1.0) < 0.1 and a["zero_code"] > 128
    assert any("remote_acquired" in c for c in r["caveats"])
    r = await client.call("scope_set_channel", channel="CH1", range_index=6, probe=1)
    assert r["applied"]["software_zoom"] is True
    assert "probe" in await client.fails("scope_set_channel", channel="CH1", probe=3)


async def test_relay_rate_limit(sim, tmp_path):
    async with client_for(sim, tmp_path, relay_changes_per_s=2) as c:
        waits = []
        for rng in (1, 2, 3, 4):
            r = await c.call("scope_set_channel", channel="CH1", range_index=rng)
            waits.append(any("rate limit" in x for x in r["caveats"]))
        assert waits[:2] == [False, False] and any(waits[2:])
        assert sim.device.relay_switches == 4


async def test_set_timebase_variants(client):
    r = await client.call("scope_set_timebase", sample_rate="3MS/s")
    assert r["applied"]["divider"] == 65 and r["applied"]["sample_rate"] >= 3e6
    r = await client.call("scope_set_timebase", divider=0)
    assert r["applied"]["sample_rate"] == 200e6
    await client.call("scope_set_trigger", total_samples=10000, pretrigger_fraction=0.5)
    r = await client.call("scope_set_timebase", window="1ms")
    assert r["applied"]["window"] >= 1e-3 and r["applied"]["sample_rate"] == 10e6
    assert "exactly one" in await client.fails("scope_set_timebase", sample_rate=1e6, divider=3)


async def test_set_trigger_volts_and_validation(client):
    await client.call("scope_set_channel", channel="CH2", volts_per_div=1.0)
    r = await client.call("scope_set_trigger", source="CH2", edge="falling", level="1.5V", mode="single",
                          pretrigger_fraction=0.25, total_samples=8000, auto_timeout="1ms")
    a = r["applied"]
    assert a["source"] == "CH2" and a["edge"] == "falling" and a["pretrigger_samples"] == 2000
    assert abs(a["level_volts"] - 1.5) <= a["level_step_volts"]
    assert any("single" in x for x in r["caveats"])
    msg = await client.fails("scope_set_trigger", pretrigger_samples=8000, total_samples=8000)
    assert "Lower pretrigger_fraction" in msg
    assert "EXT" in await client.fails("scope_set_trigger", source="EXT")
    assert "16382" in await client.fails("scope_set_trigger", total_samples=20000)


async def test_capture_inline_window_and_file(client):
    await client.call("scope_set_channel", channel="CH1", volts_per_div=0.1)
    await client.call("scope_set_timebase", sample_rate=1e6)
    await client.call("scope_set_trigger", source="CH1", edge="rising", level=0.0, mode="normal",
                      pretrigger_fraction=0.5, total_samples=10000)
    r = await client.call("scope_capture", channels=["CH1"], raw=True)
    assert r["trigger"] == "fired" and r["n_samples"] == 10000 and "preview" in r
    ch = r["channels"]["CH1"]
    assert abs(ch["frequency"] - 10e3) / 10e3 < 0.01 and abs(ch["vpp"] - 0.5) < 0.03
    data = np.load(r["file"])
    assert "CH1_raw_a" in data and data["t"].size == 10000
    meta = json.loads(str(data["meta_json"]))
    assert meta["pretrigger_samples"] == 5000
    w = await client.call("scope_capture", channels=["CH1"], window=["-100us", "100us"])
    assert w["n_samples"] <= 2000 and "samples" in w and abs(w["t_start"] + 100e-6) < 2e-6
    p = await client.call("scope_capture", channels=["CH1"], processed=True)
    assert p["n_samples"] == 10000


async def test_capture_caveats_clipping_and_no_trigger(client):
    msg = await client.fails("scope_set_channel", channel="CH1", range_index=5, offset_div=4)
    assert "out of reach" in msg
    await client.call("scope_set_channel", channel="CH1", range_index=5, offset_div=3)  # pushes the sine into 255
    await client.call("scope_set_trigger", source="CH1", level_code=128, mode="auto")
    r = await client.call("scope_capture", channels=["CH1"])
    assert r["channels"]["CH1"]["clipped_samples"] > 0 and any("clipped" in x for x in r["caveats"])
    await client.call("scope_set_trigger", level_code=0, mode="normal")  # unreachable
    r = await client.call("scope_capture", channels=["CH1"], timeout_s=0.3)
    assert r["ok"] is False and r["trigger"] == "none"


async def test_acquisition_control_and_fetch(client):
    await client.call("scope_set_trigger", mode="auto", level_code=128)
    r = await client.call("scope_acquisition_control", action="arm")
    assert r["acq_state"] in ("ARMED", "DONE")
    r = await client.call("scope_acquisition_control", action="wait", timeout_s=1)
    assert r["acq_state"] == "DONE"
    f = await client.call("scope_fetch_waveform", channels=["CH1", "CH2"])
    assert set(f["channels"]) == {"CH1", "CH2"}
    await client.call("scope_set_trigger", mode="normal", level_code=255)
    await client.call("scope_acquisition_control", action="arm")
    r = await client.call("scope_acquisition_control", action="force")
    assert r["acq_state"] == "DONE" and "AUTO_TIMEOUT" in r["flags"]
    await client.call("scope_acquisition_control", action="arm")
    r = await client.call("scope_acquisition_control", action="stop")
    assert r["acq_state"] == "ABORTED"


async def test_measure(client):
    await client.call("scope_set_channel", channel="CH1", volts_per_div=0.1)
    await client.call("scope_set_channel", channel="CH2", volts_per_div=1.0)
    await client.call("scope_set_timebase", sample_rate=2e6)
    await client.call("scope_set_trigger", source="CH1", level=0.0, mode="normal", total_samples=8000,
                      pretrigger_fraction=0.5)
    r = await client.call("scope_measure", capture=True, items=["frequency", "duty", "vpp", "rise_time", "delay"])
    assert abs(r["channels"]["CH1"]["frequency"] - 1e4) < 100
    assert abs(r["channels"]["CH2"]["duty"] - 0.5) < 0.02
    # CH2 is a sync square in phase with the sine: rising mid crossings coincide within a sample or two
    assert abs(r["delay_ch1_to_ch2"]) < 2e-6
    r2 = await client.call("scope_measure", items=["vpp"], channels=["CH1"])
    assert r2["capture_id"] == r["capture_id"]


async def test_capture_and_fft_peak(client):
    await client.call("scope_set_channel", channel="CH1", volts_per_div=0.1)
    await client.call("scope_set_timebase", sample_rate=1e6)
    await client.call("scope_set_trigger", mode="auto", total_samples=16000, pretrigger_fraction=0.1)
    r = await client.call("scope_capture_and_fft", channel="CH1", n_avg=4, window_fn="flattop", fmax="100kHz")
    pk = r["peaks"][0]
    assert abs(pk["frequency"] - 10e3) < r["frequency_resolution"]
    # 0.25 V peak sine = 0.177 V rms = -15.05 dBV
    assert abs(pk["dbv"] - 20 * math.log10(0.25 / math.sqrt(2))) < 0.5
    assert r["thd_percent"] is not None and r["thd_percent"] < 5
    assert any("SFDR" in c for c in r["caveats"])
    assert np.load(r["file"])["f"].size > 1000


# --- averaging and decay --------------------------------------------------------------------------------


async def test_averaging_reduces_noise_by_sqrt_n(sim_factory, tmp_path):
    srv = sim_factory(signal="dc:v=0.05", noise_lsb=1.5)
    async with client_for(srv, tmp_path) as c:
        await c.call("scope_set_channel", channel="CH1", volts_per_div=0.1)
        await c.call("scope_set_trigger", mode="auto", total_samples=4000, pretrigger_fraction=0.5)
        single = await c.call("scope_capture", channels=["CH1"])
        lsb = single["channels"]["CH1"]["volts_per_lsb"]
        noise1 = single["channels"]["CH1"]["ac_rms"] / lsb
        n = 64
        r = await c.call("scope_averaged_capture", n=n, channels=["CH1"], reject_clipped=True, on_device=False)
        assert r["n_used"] == n and r["averaging"] == "host"
        mean = np.load(r["file"])["CH1_mean"]
        noise_n = float(np.std(mean)) / lsb
        ratio = noise1 / noise_n
        assert math.sqrt(n) * 0.6 < ratio < math.sqrt(n) * 1.5, (noise1, noise_n, ratio)
        assert abs(r["noise_single_capture_lsb"] - 1.5) < 0.5
        assert r["effective_bits_estimate"] == pytest.approx(8 + 0.5 * math.log2(n))
        # device averaging path (ACQUIRE_AVG) when clipped captures need not be rejected
        d = await c.call("scope_averaged_capture", n=n, channels=["CH1"], reject_clipped=False, window=["-50us", "50us"])
        assert d["averaging"] == "device" and d["n_used"] == n
        mean_d = np.load(d["file"])["CH1_mean"]
        assert abs(float(mean_d.mean()) - 0.05) < 2 * lsb


async def test_edge_alignment_removes_trigger_jitter(sim_factory, tmp_path):
    """1 MHz sine at 20 MS/s: with hardware alignment the 2-sample trigger quantisation shows up as a large
    per-sample spread on the slope; edge alignment (sub-sample shift) removes it."""
    srv = sim_factory(signal="sine:freq=1MHz,amp=0.3", noise_lsb=1.0)
    async with client_for(srv, tmp_path) as c:
        await c.call("scope_set_channel", channel="CH1", volts_per_div=0.1)
        await c.call("scope_set_timebase", sample_rate=20e6)
        await c.call("scope_set_trigger", source="CH1", edge="rising", level=0.0, mode="normal", total_samples=2000,
                     pretrigger_fraction=0.5)
        hw = await c.call("scope_averaged_capture", n=48, window=["-100ns", "100ns"], on_device=False)
        ed = await c.call("scope_averaged_capture", n=48, window=["-100ns", "100ns"], align="edge",
                          align_channel="CH1", align_level=0.0)
        es = ed["edge_shift_samples"]
        assert 0.3 < es["std"] < 0.9 and es["max"] - es["min"] <= 2.5  # trigger quantised to 2 samples
        sh = np.load(hw["file"])["CH1_std"]
        se = np.load(ed["file"])["CH1_std"]
        assert sh.max() > 0.03 and se.max() < sh.max() / 3, (sh, se)


async def test_decay_fit_recovers_tau(sim_factory, tmp_path):
    srv = sim_factory(signal="decay:tau=80us,amp=0.3,period=1ms,width=200us", noise_lsb=1.0)
    async with client_for(srv, tmp_path) as c:
        await setup_decay(c)
        r = await c.call("scope_decay_fit", channel="CH1", edge_channel="CH2", t_start="5us", t_end="400us",
                         model="exp+offset", n_avg=16, sample_points=["10us", "50us", "100us"])
        assert r["ok"] and r["alignment"] == "edge"
        assert abs(r["tau"] - 80e-6) / 80e-6 < 0.03, r
        assert abs(r["amplitude"] - 0.3) < 0.02 and abs(r["offset"]) < 0.005 and r["r2"] > 0.999
        sp = {round(p["delay"] * 1e6): p["value"] for p in r["sample_points"]}
        assert abs(sp[50] - 0.3 * math.exp(-51e-6 / 80e-6)) < 0.01
        f = np.load(r["file"])
        assert {"t", "data", "fit", "residual"} <= set(f.files)
        # fixed baseline and the plain exp model
        r2 = await c.call("scope_decay_fit", model="exp", t_start="5us", t_end="400us", n_avg=8,
                          baseline=["600us", "750us"])
        assert abs(r2["tau"] - 80e-6) / 80e-6 < 0.05


async def test_decay_fit_two_exponentials_and_blanking(sim_factory, tmp_path):
    srv = sim_factory(signal="decay:tau=100us,amp=0.2,tau2=10us,amp2=0.4,flyback=5,period=1ms,width=200us",
                      noise_lsb=0.8)
    async with client_for(srv, tmp_path) as c:
        await setup_decay(c)
        r = await c.call("scope_decay_fit", model="exp2", t_start="2us", t_end="500us", n_avg=32,
                         blank_after_clip="3us")
        assert r["excluded_samples"] > 0  # the flyback spike clipped
        assert abs(r["tau"] - 10e-6) / 10e-6 < 0.15 and abs(r["tau2"] - 100e-6) / 100e-6 < 0.08, r


async def test_decay_fit_hardware_alignment(sim_factory, tmp_path):
    srv = sim_factory(signal="decay:tau=60us,amp=0.3", noise_lsb=1.0)
    async with client_for(srv, tmp_path) as c:
        await setup_decay(c)
        r = await c.call("scope_decay_fit", edge_channel="none", t_start="5us", t_end="300us", n_avg=8)
        assert r["alignment"] == "hardware" and abs(r["tau"] - 60e-6) / 60e-6 < 0.05


# --- tier 3/4 ---------------------------------------------------------------------------------------------


async def test_generator_confirmation_flow(sim_factory, tmp_path):
    srv = sim_factory()
    async with client_for(srv, tmp_path) as c:
        r = await c.call("scope_generator", enabled=True, frequency="10kHz", duty_percent=30)
        assert r["confirmation_required"] and "10 kHz" in r["summary"] and "30.0 % duty" in r["summary"]
        assert srv.device.settings.gen.enable == 0
        tok = r["token"]
        r2 = await c.call("scope_generator", enabled=True, frequency="20kHz", duty_percent=30, confirm_token=tok)
        assert r2["confirmation_required"] and r2["reason"] == "token was issued for different arguments"
        r3 = await c.call("scope_generator", enabled=True, frequency="20kHz", duty_percent=30,
                          confirm_token=r2["token"])
        assert r3["ok"] and abs(r3["applied"]["frequency"] - 20e3) < 0.02 and r3["applied"]["duty_percent"] == 30
        assert srv.device.settings.gen.enable == 1
        r4 = await c.call("scope_generator", enabled=False)  # no token needed
        assert r4["ok"] and srv.device.settings.gen.enable == 0
        assert "ceiling" in await c.fails("scope_generator", enabled=True, frequency=1e3, duty_percent=80)
        assert "FNIRSI_MCP_GEN_MAX_HZ" in await c.fails("scope_generator", enabled=True, frequency="2MHz")
        lines = (tmp_path / "audit.jsonl").read_text().splitlines()
        assert any('"scope_generator"' in ln and '"ok"' in ln for ln in lines)


async def test_generator_unavailable_on_stock(sim_factory, tmp_path):
    async with client_for(sim_factory(variant="stock"), tmp_path) as c:
        assert "no programmable generator" in await c.fails("scope_generator", enabled=True, frequency=1e3)
        assert "fixed geometry" in await c.fails("scope_set_trigger", total_samples=8000)


async def test_self_test_loopback(sim_factory, tmp_path):
    srv = sim_factory(signal="gen", noise_lsb=1.0)
    async with client_for(srv, tmp_path) as c:
        r = await c.call("scope_self_test_loopback")
        assert r["confirmation_required"]
        r = await c.call("scope_self_test_loopback", confirm_token=r["token"])
        failed = [x for x in r["checks"] if not x["pass"]]
        assert r["passed"], failed
        names = " ".join(x["check"] for x in r["checks"])
        assert "T4" in names and "T5" in names and "T7" in names
        assert srv.device.settings.gen.enable == 0  # restored


async def test_self_test_detects_wrong_interleave(sim_factory, tmp_path):
    srv = sim_factory(signal="gen", noise_lsb=0.5, true_order="a_first")
    async with client_for(srv, tmp_path) as c:
        tok = (await c.call("scope_self_test_loopback"))["token"]
        r = await c.call("scope_self_test_loopback", confirm_token=tok)
        t4 = next(x for x in r["checks"] if x["check"].startswith("T4"))
        assert not t4["pass"] and t4["measured_order"] == "A before B"


async def test_raw_fpga_and_enter_fel(client):
    r = await client.call("scope_raw_fpga", ops=[{"cmd": 6}, {"read": 2}])
    assert r["confirmation_required"]
    r = await client.call("scope_raw_fpga", ops=[{"cmd": 6}, {"read": 2}], confirm_token=r["token"])
    assert r["read"] == [0x15, 0x32]
    r = await client.call("scope_enter_fel")
    r = await client.call("scope_enter_fel", confirm_token=r["token"])
    assert r["ok"]
    info = await client.call("scope_info")  # the server reconnects (the simulator keeps listening)
    assert info["fpga_version"] == "0x1532"


async def test_user_tap_makes_next_call_fail(client):
    await client.call("scope_remote_session", action="acquire")
    client.srv.device.user_tap()
    msg = await client.fails("scope_set_timebase", sample_rate=1e6)
    assert "released by user" in msg
    r = await client.call("scope_set_timebase", sample_rate=1e6)  # an explicit retry re-acquires
    assert any("remote_acquired" in x for x in r["caveats"])


async def test_watchdog_reacquire_note(sim, tmp_path):
    async with client_for(sim, tmp_path, watchdog_s=1) as c:
        await c.call("scope_set_timebase", sample_rate=1e6)
        import anyio
        await anyio.sleep(1.3)
        r = await c.call("scope_set_timebase", sample_rate=2e6)
        assert any("WATCHDOG" in x for x in r["caveats"])


async def test_errors_are_actionable(client):
    msg = await client.fails("scope_capture", window=["1s", "2s"])
    assert "outside the capture" in msg
    msg = await client.fails("scope_averaged_capture", n=2, window=["0", "-1us"])
    assert "after start" in msg


async def test_response_cap_keeps_tools_working(sim_factory, tmp_path):
    """Firmware 0.1.0 dropped responses over 5 KiB; with max_response the server splits reads and device
    averaging falls back to the host when its result would not fit."""
    srv = sim_factory(signal="decay:tau=80us,amp=0.3", fail_responses_over=5120)
    async with client_for(srv, tmp_path, max_response=5120) as c:
        await setup_decay(c)
        r = await c.call("scope_capture", channels=["CH1", "CH2"])
        assert r["n_samples"] == 16000
        r = await c.call("scope_decay_fit", edge_channel="CH2", t_start="5us", t_end="400us", n_avg=8)
        assert abs(r["tau"] - 80e-6) / 80e-6 < 0.03
        r = await c.call("scope_averaged_capture", n=4, reject_clipped=False, on_device=True)
        assert r["averaging"] == "host" and any("does not fit" in x for x in r["caveats"])
