"""The host-side data tools end to end on the simulator: scope_decode (UART / SPI / I2C, from a capture, a file and
the scope's latest capture), scope_mask_test (reference, tolerances, pass / fail, mask reuse), scope_export_csv
(header, columns, raw codes, segments) and scope_dvm (readings and the resolution statement); plus the CSV exporter
and scope_report on the new kinds."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from fnirsi1013d import export as X
from fnirsi1013d import report as R
from fnirsi1013d.simulator import parse_signal
from test_server import client_for

pytestmark = pytest.mark.anyio


@pytest.fixture
def anyio_backend():
    return "asyncio"


async def logic_setup(c, rate="2MS/s", total=2000, pre=100, source="CH1", level=1.5, edge="falling"):
    """Both channels on 1 V/div DC (3.3 V logic fits), a normal trigger on the first edge of the message."""
    for ch in ("CH1", "CH2"):
        await c.call("scope_set_channel", channel=ch, volts_per_div=1.0, coupling="DC", offset_div=-2.0)
    await c.call("scope_set_timebase", sample_rate=rate)
    await c.call("scope_set_trigger", source=source, edge=edge, level=level, mode="normal", pretrigger_samples=pre,
                 total_samples=total)


def load_meta(path: str) -> dict:
    with np.load(path, allow_pickle=False) as z:
        return json.loads(str(z["meta_json"]))


# --- scope_decode -------------------------------------------------------------------------------------------


async def test_decode_uart_capture_file_and_latest(sim_factory, tmp_path):
    srv = sim_factory(signal="uart:baud=115200,text=Hi!,parity=1", noise_lsb=1.0)
    async with client_for(srv, tmp_path) as c:
        await logic_setup(c)
        r = await c.call("scope_decode", protocol="uart", capture=True, parity="even")
        assert r["ok"] and r["protocol"] == "uart" and r["lines"] == {"data": "CH1"}
        assert [f["char"] for f in r["frames"]] == ["H", "i", "!"] and r["n_errors"] == 0
        assert r["frames"][0]["t"] == pytest.approx(0.0, abs=2e-6)  # the trigger sits on the start bit
        assert r["applied"]["baud"] == 115200 and r["applied"]["auto_baud"] is True
        assert r["applied"]["samples_per_bit"] == pytest.approx(2e6 / 115200, rel=0.01)
        assert r["preview"][0].endswith("0x48 'H'") and Path(r["file"]).is_file()
        assert any("auto-baud" in x for x in r["caveats"]) and any("hysteresis" in x for x in r["caveats"])
        assert any("scope is now in STOP" in x for x in r["caveats"])
        meta = load_meta(r["file"])
        assert meta["kind"] == "decode" and len(meta["frames"]) == 3 and meta["channels"]["CH1"]["range"] == 2
        # the same capture from its file (tier 0), with a given baud and the parity left out: parity bit = framing
        cap = await c.call("scope_capture", channels=["CH1"])
        r2 = await c.call("scope_decode", protocol="uart", source=cap["file"], baud="115200")
        assert [f["errors"] for f in r2["frames"]] == [["framing"]] * 3 and r2["n_errors"] == 3
        r3 = await c.call("scope_decode", protocol="uart", source="latest", parity="even")
        assert r3["n_errors"] == 0 and r3["applied"]["auto_baud"]
        # the scope's latest capture without a source
        r4 = await c.call("scope_decode", protocol="uart", parity="even")
        assert [f["char"] for f in r4["frames"]] == ["H", "i", "!"]
        # a report of the decode result
        rep = await c.call("scope_report", source=r["file"], title="UART")
        assert rep["kind"] == "decode" and rep["table"][0]["value"] == "0x48" and "UART: 3 frame(s)" in rep["summary"][0]
        # wrong arguments
        assert "needs 2 channel" in await c.fails("scope_decode", protocol="spi", channels=["CH1"])
        assert "either source" in await c.fails("scope_decode", protocol="uart", source=cap["file"], capture=True)
        assert "no such file" in await c.fails("scope_decode", protocol="uart", source="nope.npz")


async def test_decode_uart_too_slow_sample_rate(sim_factory, tmp_path):
    srv = sim_factory(signal="uart:baud=1MHz,text=Hi!", noise_lsb=1.0)
    async with client_for(srv, tmp_path) as c:
        await logic_setup(c, rate="2.5MS/s", total=400, pre=20)
        r = await c.call("scope_decode", protocol="uart", capture=True, baud="1MHz")
        assert any("only 2.5 samples per bit" in x for x in r["caveats"])
        await c.call("scope_set_timebase", sample_rate="1.5MS/s")
        msg = await c.fails("scope_decode", protocol="uart", capture=True, baud="1MHz")
        assert "samples per bit" in msg and "raise the sample rate" in msg


async def test_decode_spi_and_i2c(sim_factory, tmp_path):
    spi = sim_factory(signal="spi_clk:freq=1MHz,text=Hi!,cpol=1,cpha=1", noise_lsb=1.0)
    async with client_for(spi, tmp_path / "spi") as c:
        await logic_setup(c, rate="20MS/s", total=1000, pre=50, source="CH1", edge="falling")
        r = await c.call("scope_decode", protocol="spi", capture=True, cpol=1, cpha=1)
        assert r["lines"] == {"clock": "CH1", "data": "CH2"}
        assert [w["char"] for w in r["frames"]] == ["H", "i", "!"] and r["n_errors"] == 0
        assert r["applied"]["clock_hz"] == pytest.approx(1e6, rel=0.02) and r["applied"]["sampling_edge"] == "rising"
        assert any("no chip select" in x for x in r["caveats"])
        rep = await c.call("scope_report", source=r["file"])
        assert rep["kind"] == "decode" and rep["table"][1]["value"] == "0x69" and rep["table"][0]["transfer"] == 0
    i2c = sim_factory(signal="i2c_scl:freq=400kHz,addr=60,text=Hi!,rw=1", noise_lsb=1.0)
    async with client_for(i2c, tmp_path / "i2c") as c:
        # trigger on SDA (CH2) falling: the START condition
        await logic_setup(c, rate="10MS/s", total=1200, pre=50, source="CH2", edge="falling")
        r = await c.call("scope_decode", protocol="i2c", capture=True)
        assert r["lines"] == {"scl": "CH1", "sda": "CH2"} and r["n_frames"] == 1
        f = r["frames"][0]
        assert f["address"] == "0x3C" and f["rw"] == "read" and f["ack"] and f["stop"]
        assert [d["value"] for d in f["data"]] == [0x48, 0x69, 0x21] and [d["ack"] for d in f["data"]] == [True, True, False]
        assert f["t"] == pytest.approx(0.0, abs=1e-6)
        assert r["preview"][0].endswith("0x21 NACK P") and r["applied"]["scl_hz"] == pytest.approx(400e3, rel=0.03)
        rep = await c.call("scope_report", source=r["file"])
        assert rep["kind"] == "decode" and rep["table"][0]["address"] == "0x3C" and rep["table"][0]["stop"] == "P"
        # SCL and SDA swapped: every clock reads as a START / STOP, the frames carry errors
        r = await c.call("scope_decode", protocol="i2c", channels=["CH2", "CH1"])
        assert r["n_errors"] >= 1 and r["lines"] == {"scl": "CH2", "sda": "CH1"}


async def test_decode_is_tier_0_without_capture(sim_factory, tmp_path):
    srv = sim_factory(signal="uart:baud=115200,text=Hi!", noise_lsb=1.0)
    async with client_for(srv, tmp_path, max_tier=0, enable_raw=False) as c:
        names = {t.name for t in (await c.session.list_tools()).tools}
        assert {"scope_decode", "scope_export_csv"} <= names and "scope_mask_test" not in names
        assert "needs safety tier 1" in await c.fails("scope_decode", protocol="uart", capture=True)
        srv.device.ui_run_capture()
        r = await c.call("scope_decode", protocol="uart")  # the scope's own latest capture
        assert r["ok"] and r["n_frames"] >= 1


# --- scope_mask_test ----------------------------------------------------------------------------------------


async def test_mask_test_pass_fail_reuse_and_report(sim_factory, tmp_path):
    srv = sim_factory(signal="square:freq=10kHz,amp=0.3", noise_lsb=1.0)
    async with client_for(srv, tmp_path) as c:
        await c.call("scope_set_channel", channel="CH1", volts_per_div=0.1, coupling="DC")
        await c.call("scope_set_timebase", sample_rate="20MS/s")
        await c.call("scope_set_trigger", source="CH1", edge="rising", level=0.0, mode="normal",
                     pretrigger_samples=200, total_samples=4000)
        r = await c.call("scope_mask_test", channel="CH1", n=5, tolerance_div=0.5, tolerance_time="200ns",
                         mask_name="sq10k")
        assert r["ok"] and r["all_passed"] and r["passed"] == 5 and r["failed"] == 0 and r["n_tested"] == 5
        assert r["tolerance"]["volts"] == pytest.approx(0.05) and r["tolerance"]["samples"] == 4
        assert r["mask"]["file"].endswith("sq10k.npz") and Path(r["mask"]["file"]).is_file()
        assert r["mask"]["built_from"].startswith("reference capture") and "last passing" in r["shown_in_file"]
        meta = load_meta(r["file"])
        assert meta["kind"] == "mask_test" and len(meta["results"]) == 5 and meta["tolerance"]["samples"] == 4
        rep = await c.call("scope_report", source=r["file"])
        assert rep["kind"] == "mask_test" and len(rep["table"]) == 5 and "5 passed, 0 failed" in rep["summary"][1]
        # a tighter mask fails on the noise and the caveat says why
        r = await c.call("scope_mask_test", channel="CH1", n=3, tolerance_volts="1mV", tolerance_time=0)
        assert not r["all_passed"] and r["failed"] >= 1
        assert any("below about 2 codes" in x for x in r["caveats"]) and any("under 2 samples" in x for x in r["caveats"])
        assert r["first_failure"]["first_fail"]["bound"] in ("upper", "lower")
        # the signal changes: the stored mask catches it
        srv.device.cfg.signals = (parse_signal("square:freq=10kHz,amp=0.42"), parse_signal("zero"))
        r = await c.call("scope_mask_test", channel="CH1", reference="sq10k", n=4, stop_on_fail=True)
        assert r["failed"] == 1 and r["n_tested"] == 1 and r["mask"]["built_from"].startswith("mask file")
        ff = r["results"][0]["first_fail"]
        assert (ff["volts"] > ff["limit"]) if ff["bound"] == "upper" else (ff["volts"] < ff["limit"])
        assert "first failing" in r["shown_in_file"] and r["results"][0]["worst_excursion_volts"] > 0.05
        rep = await c.call("scope_report", source=r["file"])
        assert rep["table"][0]["passed"] is False and "first fail at" in rep["table"][0]
        # from a saved capture file, with the tolerance in volts
        srv.device.cfg.signals = (parse_signal("square:freq=10kHz,amp=0.3"), parse_signal("zero"))
        cap = await c.call("scope_capture", channels=["CH1"])
        r = await c.call("scope_mask_test", channel="CH1", reference=cap["file"], tolerance_volts="60mV", n=2)
        assert r["all_passed"] and r["mask"]["built_from"].startswith("saved capture")
        # geometry mismatch is refused with the remedy
        await c.call("scope_set_trigger", total_samples=2000)
        msg = await c.fails("scope_mask_test", channel="CH1", reference="sq10k", n=1)
        assert "4000 samples" in msg and "2000 samples" in msg and "scope_set_trigger" in msg
        assert "stored masks: sq10k" in await c.fails("scope_mask_test", reference="nope", n=1)


async def test_mask_test_stops_when_settings_change(sim_factory, tmp_path):
    srv = sim_factory(signal="square:freq=10kHz,amp=0.3", noise_lsb=1.0)
    async with client_for(srv, tmp_path) as c:
        await c.call("scope_set_channel", channel="CH1", volts_per_div=0.1, coupling="DC")
        await c.call("scope_set_trigger", source="CH1", edge="rising", level=0.0, mode="normal",
                     pretrigger_samples=100, total_samples=1000)
        n0 = srv.device.arm_count
        # the reference and one test capture are in; then the person changes the range
        srv.device.on_arm = lambda d: d.ui_set_channel(0, range=4) if d.arm_count == n0 + 3 else None
        r = await c.call("scope_mask_test", channel="CH1", n=6)
        assert r["n_tested"] == 1 and any("settings were changed at the scope" in x for x in r["caveats"])


# --- scope_export_csv ---------------------------------------------------------------------------------------


async def test_export_csv_capture_raw_and_segments(sim_factory, tmp_path):
    srv = sim_factory(signal="square:freq=10kHz,amp=0.3", noise_lsb=1.0)
    async with client_for(srv, tmp_path) as c:
        await c.call("scope_set_channel", channel="CH1", volts_per_div=0.1, coupling="DC")
        await c.call("scope_set_timebase", sample_rate="20MS/s")
        await c.call("scope_set_trigger", source="CH1", edge="rising", level=0.0, mode="normal",
                     pretrigger_samples=32, total_samples=240)
        cap = await c.call("scope_capture", channels=["CH1", "CH2"], raw=True)
        r = await c.call("scope_export_csv", source=cap["file"], caveats=["probe x1"])
        assert r["ok"] and r["kind"] == "capture" and r["rows"] == 240
        assert r["columns"] == ["t", "CH1_volts", "CH1_clipped", "CH2_volts", "CH2_clipped"]
        assert r["units"]["t"] == "s" and r["units"]["CH1_volts"] == "V"
        text = Path(r["file"]).read_text(encoding="utf-8")
        head = [ln for ln in text.splitlines() if ln.startswith("#")]
        assert head[0].startswith("# fnirsi1013d Capture export of") and any("sample_rate 20000000 Sa/s" in h for h in head)
        assert any(h.startswith("# CH1: 0.1 V/div, DC, probe x1") for h in head)
        assert any("# trigger: source 0" in h for h in head) and any("# caveat: probe x1" in h for h in head)
        assert any(h.startswith("# scaling:") for h in head) and head[-1].startswith("# columns: t (s), CH1_volts (V)")
        body = [ln for ln in text.splitlines() if not ln.startswith("#")]
        assert body[0] == "t,CH1_volts,CH1_clipped,CH2_volts,CH2_clipped" and len(body) == 241
        vals = body[1].split(",")
        assert float(vals[0]) == pytest.approx(-32 * 50e-9) and vals[2] in ("0", "1")
        # raw codes, another delimiter and an explicit output path
        out = tmp_path / "x" / "cap.csv"
        r = await c.call("scope_export_csv", source=cap["file"], raw=True, delimiter=";", out=str(out))
        assert r["file"] == str(out) and "CH1_codes" in r["columns"] and "CH1_raw_a" not in r["columns"]
        assert any("*_raw_a / *_raw_b" in h for h in r["header"])  # half-length per-core arrays stay in the .npz
        assert Path(out).read_text(encoding="utf-8").splitlines()[-1].count(";") == len(r["columns"]) - 1
        # 'latest' = the newest .npz; an averaged capture gives mean / std columns
        avg = await c.call("scope_averaged_capture", n=3, channels=["CH1"])
        r = await c.call("scope_export_csv")
        assert r["source"] == avg["file"] and r["columns"][:3] == ["t", "CH1_mean", "CH1_std"]
        # segments: long format
        seg = await c.call("scope_capture_segments", count=4, channels=["CH1"])
        r = await c.call("scope_export_csv", source=seg["file"])
        assert r["columns"][:3] == ["segment", "t", "CH1_volts"] and "timestamps_s" in r["columns"]
        assert r["rows"] == 4 * 240
        # spectrum
        fft = await c.call("scope_capture_and_fft", channel="CH1")
        r = await c.call("scope_export_csv", source=fft["file"])
        assert r["columns"] == ["f", "amplitude_vrms"] and r["units"]["f"] == "Hz"
        assert "no such file" in await c.fails("scope_export_csv", source="nope.npz")


def test_write_csv_units_and_errors(tmp_path):
    t = np.arange(10) * 1e-6
    p = tmp_path / "a.npz"
    np.savez_compressed(p, meta_json=np.array(json.dumps({"dt": 1e-6, "kind": "capture", "caveats": ["x"]})), t=t,
                        CH1_volts=np.sin(t), CH1_clipped=np.zeros(10, bool), h=np.exp(1j * t))
    res = X.write_csv(p, raw=True)
    assert res["columns"] == ["t", "CH1_volts", "CH1_clipped", "h_re", "h_im"] and res["caveats"] == ["x"]
    assert Path(res["file"]) == p.with_suffix(".csv")
    assert X.unit_of("CH2_mean") == "V" and X.unit_of("mag_db") == "dB" and X.unit_of("foo") == ""
    q = tmp_path / "b.npz"
    np.savez_compressed(q, meta_json=np.array("{}"), foo=np.zeros(3))
    with pytest.raises(R.ReportError):
        X.write_csv(q)


# --- scope_dvm ----------------------------------------------------------------------------------------------


async def test_dvm_dc_level_with_noise(sim_factory, tmp_path):
    srv = sim_factory(noise_lsb=1.2)
    srv.device.cfg.signals = (parse_signal("dc:v=0.25"), parse_signal("sine:freq=1kHz,amp=0.1,offset=-0.05"))
    async with client_for(srv, tmp_path) as c:
        for ch in ("CH1", "CH2"):
            await c.call("scope_set_channel", channel=ch, volts_per_div=0.1, coupling="DC")
        await c.call("scope_set_timebase", sample_rate="2MS/s")
        await c.call("scope_set_trigger", source="CH2", edge="rising", level=-0.05, mode="normal",
                     pretrigger_samples=0, total_samples=4000)  # 2 ms = two periods of the 1 kHz sine
        r = await c.call("scope_dvm", channels=["CH1", "CH2"], n=6)
        assert r["ok"] and r["n_captures"] == 6 and r["n_samples_total"] == 24000 and r["restarts"] == 0
        ch1, ch2 = r["channels"]["CH1"], r["channels"]["CH2"]
        assert ch1["dc_mean"] == pytest.approx(0.25, abs=0.004) and ch1["ac_rms"] < 0.01 and ch1["frequency"] is None
        assert ch1["range"]["volts_per_div"] == 0.1 and ch1["range"]["coupling"] == "DC"
        res = ch1["resolution"]
        assert res["dither"] is True and 0.9 < res["noise_codes"] < 1.6
        assert res["uncertainty_volts"] < 0.0005 and 3.5 < res["digits"] < 5.0  # the noise averaged over 24000 samples
        assert "digits of full scale" in ch1["statement"] and "absolute accuracy" in ch1["statement"]
        assert ch2["dc_mean"] == pytest.approx(-0.05, abs=0.004) and ch2["ac_rms"] == pytest.approx(0.1 / 2 ** 0.5, rel=0.05)
        assert ch2["rms"] == pytest.approx((0.05 ** 2 + 0.1 ** 2 / 2) ** 0.5, rel=0.05)
        assert ch2["frequency"] == pytest.approx(1e3, rel=0.03) and ch2["vpp"] == pytest.approx(0.2, abs=0.03)
        assert any("whole periods" in x for x in r["caveats"]) and r["statements"][1].startswith("CH2: DC")
        meta = load_meta(r["file"])
        assert meta["kind"] == "dvm" and meta["n_captures"] == 6 and "CH1" in meta["readings"]
        rep = await c.call("scope_report", source=r["file"])
        assert rep["kind"] == "dvm" and rep["table"][0]["channel"] == "CH1" and "6 captures" in rep["summary"][0]
        csv = await c.call("scope_export_csv", source=r["file"])
        assert csv["columns"][:2] == ["capture_t_s", "CH1_mean"] and csv["rows"] == 6
        # by duration
        r = await c.call("scope_dvm", channels=["CH1"], duration_s=0.3)
        assert r["n_captures"] >= 1 and r["elapsed_s"] >= 0.3


async def test_dvm_without_dither_says_so(sim_factory, tmp_path):
    srv = sim_factory(noise_lsb=0.0)
    srv.device.cfg.signals = (parse_signal("dc:v=0.2"), parse_signal("zero"))
    async with client_for(srv, tmp_path) as c:
        await c.call("scope_set_channel", channel="CH1", volts_per_div=0.1, coupling="DC")
        await c.call("scope_set_trigger", source="CH1", mode="auto", total_samples=1000)
        r = await c.call("scope_dvm", channels=["CH1"], n=2)
        res = r["channels"]["CH1"]["resolution"]
        assert res["dither"] is False and res["uncertainty_volts"] == pytest.approx(res["step_volts"] / 2)
        assert any("no dither" in x for x in r["caveats"])
        assert any("auto time-out" in x for x in r["caveats"])  # auto mode on a DC level
