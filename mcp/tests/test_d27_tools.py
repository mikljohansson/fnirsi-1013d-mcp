"""Protocol 1.27 tools (item D6): scope_sweep (the host-stepped chirp), scope_generator on the fractional square,
scope_log / scope_log_status / scope_log_stop with the files and logfile.load_log, scope_report on the 'log' kind. The
pattern tool and the 'pattern' trigger source were cut (owner 2026-10-21, fw-nopat)."""

from __future__ import annotations

import json
import time
from pathlib import Path

import anyio
import numpy as np
import pytest

from fnirsi1013d import logfile as LF
from fnirsi1013d import protocol as P
from fnirsi1013d.report import have_matplotlib
from fnirsi1013d.simulator import parse_signal
from test_server import client_for


def gen_sim(sim_factory, ch2: str = "zero", **kw):
    return sim_factory(signals=(parse_signal("gen"), parse_signal(ch2)), **kw)


def test_plan_log_rates_and_tiers():
    p = LF.plan_log(500e3, "CH1", 8, "auto")
    assert (p.fmt, p.divider, p.filter, p.rate) == (P.LOG_FMT_HIGH8, 399, "cic3+fir", 500e3)
    p = LF.plan_log(250e3, ["CH1", "CH2"], 8, "auto")
    assert p.channels == 3 and p.rate == 250e3 and p.bytes_per_s == 500e3
    p = LF.plan_log(125e3, ["CH1", "CH2"], 16, "auto")
    assert p.fmt == P.LOG_FMT_WIDE16 and p.divider == 1599
    p = LF.plan_log(100e3, "CH2", 8, "off")
    assert p.fmt == P.LOG_FMT_RAW8 and p.divider == 1999 and p.channels == 2 and LF.RAW8_NOTE in p.caveats
    p = LF.plan_log(48e3, "CH1", 16, "peak")
    assert p.fmt == P.LOG_FMT_PEAK and p.filter == "peak" and abs(p.rate / 48e3 - 1) < 1e-3
    p = LF.plan_log(30e3, "CH1", 8, "auto")  # FIR-legal N (a multiple of 4)
    assert (p.divider + 1) % 4 == 0 and p.rate == pytest.approx(30e3, rel=1e-3)
    for kw in ({"rate_hz": 600e3}, {"rate_hz": 300e3, "bits": 16}, {"rate_hz": 300e3, "channels": ["CH1", "CH2"]},
               {"rate_hz": 130e3, "channels": ["CH1", "CH2"], "bits": 16}):
        with pytest.raises(ValueError, match="tiers"):
            LF.plan_log(**kw)
    with pytest.raises(ValueError):
        LF.plan_log(100e3, "CH1", 16, "off")
    data = bytes(range(8))
    d = LF.decode_chunk(data, 3, P.LOG_FMT_RAW8)
    assert d["CH1"][0].tolist() == [0, 1, 4, 5] and d["CH2"][0].tolist() == [2, 3, 6, 7]
    d = LF.decode_chunk(data, 1, P.LOG_FMT_PEAK)
    assert d["CH1"][0].tolist() == [0, 2, 4, 6] and d["CH1"][1].tolist() == [1, 3, 5, 7]
    d = LF.decode_chunk(data, 3, P.LOG_FMT_WIDE16)
    assert d["CH1"][0].tolist() == [0x0100, 0x0504] and d["CH2"][0].tolist() == [0x0302, 0x0706]


def test_log_tools_end_to_end(sim_factory, tmp_path):
    srv = sim_factory(signal="sine:freq=1kHz,amp=0.3")

    async def main():
        async with client_for(srv, tmp_path) as c:
            r = await c.call("scope_log_status")
            assert r["state"] == "none"
            assert "tiers" in await c.fails("scope_log", rate="600k")
            r = await c.call("scope_log", rate="250k", channels=["CH1"], bits=16, duration=0.4)
            assert r["applied"]["rate"] == 250e3 and r["applied"]["format"] == "filtered16"
            assert any("frozen" in x for x in r["caveats"])
            d = Path(r["directory"])
            assert "logging: LOG_STOP first" in await c.fails("scope_capture", channels=["CH1"])
            assert "a log is running" in await c.fails("scope_log", rate="100k")
            st = await c.call("scope_log_status")
            assert st["state"] in ("running", "ended") and st["plan"]["rate"] == 250e3
            await anyio.sleep(0.6)
            r = await c.call("scope_log_stop")
            assert r["state"] == "ended" and r["samples"] == 100_000 and r["gaps"] == 0
            assert r["device"]["sent"] == r["device"]["produced"] == 200_000
            assert any("set back" in x for x in r["caveats"])  # the Filter was restored
            man = json.loads((d / "log.json").read_text())
            assert man["samples"] == 100_000 and man["state"] == "ended" and man["plan"]["format"] == "filtered16"
            lg = LF.load_log(d)
            v = lg["CH1_volts"]
            assert v.size == 100_000 and lg["t"][1] == pytest.approx(4e-6)
            assert np.max(v) == pytest.approx(0.3, abs=0.03) and np.min(v) == pytest.approx(-0.3, abs=0.03)
            part = LF.load_log(d, start_s=0.2, max_samples=1000)
            assert part["t"][0] == pytest.approx(0.2, abs=1e-5) and part["CH1_volts"].size == 1000
            assert r["summary_file"] and Path(r["summary_file"]).is_file()
            rep = await c.call("scope_report", source=r["summary_file"])
            assert rep["kind"] == "log" and rep["table"][0]["channel"] == "CH1"
            if have_matplotlib():
                assert Path(rep["png"]).is_file()
            # the scope works again
            r = await c.call("scope_capture", channels=["CH1"])
            assert r["ok"]
    anyio.run(main)


def test_log_until_stopped_with_an_overflow_and_raw8(sim_factory, tmp_path):
    srv = sim_factory(signal="sine:freq=1kHz,amp=0.3", log_ring_bytes=16384, log_stall_s=0.3)

    async def main():
        async with client_for(srv, tmp_path) as c:
            r = await c.call("scope_log", rate="250k", channels=["CH1", "CH2"], bits=8, filter="off")
            assert r["applied"]["format"] == "raw8"
            await anyio.sleep(0.8)
            r = await c.call("scope_log_stop")
            assert r["state"] == "stopped" and r["gaps"] >= 1 and r["lost_samples"] > 0
            assert any("host read too slowly" in x for x in r["caveats"])
            man = json.loads((Path(r["directory"]) / "log.json").read_text())
            g = man["gaps"][0]
            assert g["cause"] == "overflow" and g["sample"] > 0
            lg = LF.load_log(r["directory"], ["CH1"])
            t = lg["t"]
            jump = np.flatnonzero(np.diff(t) > 1.5 / 250e3)
            assert jump.size == len(man["gaps"]) and t[jump[0] + 1] - t[jump[0]] == pytest.approx(
                (g["lost"] + 1) / 250e3)
    anyio.run(main)


def test_log_ended_by_a_change_at_the_scope(sim_factory, tmp_path):
    srv = sim_factory(signal="sine:freq=1kHz,amp=0.3")

    async def main():
        async with client_for(srv, tmp_path) as c:
            await c.call("scope_log", rate="100k")
            await anyio.sleep(0.2)
            srv.device.ui_set_channel(0, range=2)
            await anyio.sleep(0.2)
            r = await c.call("scope_log_stop")
            assert r["state"] == "error" and any("ended early on the scope" in x for x in r["caveats"])
            assert r["samples"] > 10_000
    anyio.run(main)


def test_generator_tool_reports_the_square(sim_factory, tmp_path):
    srv = gen_sim(sim_factory)

    async def main():
        async with client_for(srv, tmp_path) as c:
            tok = (await c.call("scope_generator", enabled=True, frequency="999.5Hz"))["token"]
            r = await c.call("scope_generator", enabled=True, frequency="999.5Hz", confirm_token=tok)
            a = r["applied"]
            assert a["frequency_exact_hz"] == pytest.approx(999.5, rel=1e-10) and "source" not in a
            assert a["period_clocks"] == pytest.approx(200_100.05, abs=1e-4) and a["p16"] == P.gen_p16(999_500)
            assert any("5 ns" in x for x in r["caveats"])
            assert "pattern" in await c.fails("scope_set_trigger", source="pattern")  # the cut marker source
    anyio.run(main)


def test_sweep_tool(sim_factory, tmp_path):
    srv = gen_sim(sim_factory)

    async def main():
        async with client_for(srv, tmp_path) as c:
            tok = (await c.call("scope_generator", enabled=True, frequency="2kHz"))["token"]
            await c.call("scope_generator", enabled=True, frequency="2kHz", confirm_token=tok)
            args = dict(start="1kHz", stop="10kHz", steps=50, time_s=0.1, shape="triangle")
            r = await c.call("scope_sweep", **args)
            assert r["confirmation_required"] and "-> back" in r["summary"]
            t0 = time.monotonic()
            r = await c.call("scope_sweep", **args, confirm_token=r["token"])
            assert time.monotonic() - t0 >= 0.19 and r["stats"]["steps"] == 99
            assert r["generator_restored"] and srv.device.settings.gen.p16 == P.gen_p16(2_000_000)
            fast = dict(start="10kHz", stop="20kHz", steps=200, rate=50_000)
            tok = (await c.call("scope_sweep", **fast))["token"]
            r = await c.call("scope_sweep", **fast, confirm_token=tok)
            assert any("2000 steps/s" in x for x in r["caveats"])
            assert "time_s" in await c.fails("scope_sweep", start="1kHz", stop="2kHz")
    anyio.run(main)
