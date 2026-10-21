"""scope_bode_sweep (MCP tool, tier 3) end to end against the simulator's RC on the generator."""

from __future__ import annotations

import json
from pathlib import Path

import anyio
import numpy as np
import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d.simulator import parse_signal
from test_server import client_for

FAST_IN, FAST_OUT = "dut_in:r=1.6k,c=1n", "dut_out:r=1.6k,c=1n"


def _sim(sim_factory, ch1=FAST_IN, ch2=FAST_OUT, **kw):
    kw.setdefault("noise_lsb", 0.5)
    srv = sim_factory(**kw)
    srv.device.cfg.signals = (parse_signal(ch1), parse_signal(ch2))
    return srv


def _rc(f):
    return 1 / (1 + 2j * np.pi * f * 1.6e3 * 1e-9)


def test_bode_tool_confirmation_result_files_and_wiring_once(sim_factory, tmp_path):
    srv = _sim(sim_factory)
    sweep = dict(start="30kHz", stop="1MHz", points=6, settle="fast")

    async def main():
        async with client_for(srv, tmp_path) as c:
            gen0 = await c.call("scope_get_config")
            gen0.pop("settings_seq", None)
            r = await c.call("scope_bode_sweep", **sweep)
            assert r["confirmation_required"] and "6 frequencies" in r["summary"]
            assert "passive DUT" in r["summary"] and "coil current" in r["summary"]
            assert "token was issued" in (await c.call("scope_bode_sweep", **{**sweep, "points": 5},
                                                       confirm_token=r["token"]))["reason"]
            r = await c.call("scope_bode_sweep", **sweep)
            res = await c.call("scope_bode_sweep", **sweep, confirm_token=r["token"])
            assert res["ok"] and res["points"] == 6 and res["stopped_early"] is None
            assert "USB ground" in res["connection"] and "clips OPEN" in res["connection"]
            rows = res["table"]
            assert [x["f_actual"] for x in rows] == sorted(x["f_actual"] for x in rows)
            for row in rows:
                h = _rc(row["f_actual"])
                assert row["mag_db"] == pytest.approx(20 * np.log10(abs(h)), abs=0.03)
                assert row["phase_deg"] == pytest.approx(np.degrees(np.angle(h)), abs=0.3)
                assert row["filter"] in ("cic3+fir", "legacy") and isinstance(row["flags"], list)
            assert res["summary"]["shape"] == "low-pass"
            # 3 dB below the first point (30 kHz, already 0.37 dB down): 1 + x^2 = 2 (1 + x0^2), x = f / fc
            fc, x0 = 1 / (2 * np.pi * 1.6e-6), rows[0]["f_actual"] * 2 * np.pi * 1.6e-6
            assert res["summary"]["minus_3db_hz"][0] == pytest.approx(fc * np.sqrt(1 + 2 * x0 * x0), rel=0.02)
            assert res["plan"]["points"] == 6 and res["plan"]["settle_periods"] == 3
            assert res["ranges"]["start"] == {"CH1": 3, "CH2": 3} and res["ranges"]["switches"] >= 1
            for k in ("csv", "json", "npz"):
                assert Path(res["files"][k]).is_file()
            z = np.load(res["files"]["npz"])
            assert z["h"].dtype == complex and z["h"].size == 6
            assert json.loads(Path(res["files"]["json"]).read_text())["summary"]["shape"] == "low-pass"
            assert any("no thru calibration" in x for x in res["caveats"])
            assert any("1 kOhm source" in x for x in res["caveats"])
            after = await c.call("scope_get_config")
            assert after.pop("settings_seq") > 0 and after == gen0  # everything given back
            # the second sweep of the session: the wiring is a one-line caveat, not the connection text
            r = await c.call("scope_bode_sweep", **{**sweep, "points": 2, "background": False})
            res2 = await c.call("scope_bode_sweep", **{**sweep, "points": 2, "background": False},
                                confirm_token=r["token"])
            assert "connection" not in res2 and any(x.startswith("wiring:") for x in res2["caveats"])
        audit = [json.loads(x) for x in (tmp_path / "audit.jsonl").read_text().splitlines()]
        assert [a["status"] for a in audit if a["tool"] == "scope_bode_sweep"][-1] == "ok"
    anyio.run(main)


def test_bode_tool_thru_measure_and_apply(sim_factory, tmp_path):
    srv = _sim(sim_factory, ch2=FAST_IN + ",gain=0.95")
    sweep = dict(start="40kHz", stop="400kHz", points=4, settle="fast", background=False)

    async def call(c, **kw):
        r = await c.call("scope_bode_sweep", **kw)
        return await c.call("scope_bode_sweep", **kw, confirm_token=r["token"])

    async def main():
        async with client_for(srv, tmp_path) as c:
            msg = await c.fails("scope_bode_sweep", **sweep, thru_cal="bench1")
            assert "no thru calibration 'bench1'" in msg and "stored: none" in msg
            t = await call(c, **sweep, thru_cal="measure", thru_name="bench1")
            assert t["thru_cal"]["stored_as"] == "bench1" and Path(t["thru_cal"]["file"]).is_file()
            assert all(x == pytest.approx(20 * np.log10(0.95), abs=0.02) for x in t["thru_cal"]["mismatch_db"])
            assert any("thru calibration: both probes" in x for x in t["caveats"])
            srv.device.cfg.signals = (parse_signal(FAST_IN), parse_signal(FAST_OUT + ",gain=0.95"))
            res = await call(c, **sweep, thru_cal="bench1")
            assert res["thru_cal"]["applied"] == "bench1"
            for row in res["table"]:
                assert row["mag_db"] == pytest.approx(20 * np.log10(abs(_rc(row["f_actual"]))), abs=0.03)
            assert not any("no thru calibration" in x for x in res["caveats"])
            assert "stored: bench1" in await c.fails("scope_bode_sweep", **sweep, thru_cal="other")
    anyio.run(main)


def test_bode_tool_refusals(sim_factory, tmp_path):
    async def main():
        async with client_for(_sim(sim_factory), tmp_path) as c:
            msg = await c.fails("scope_bode_sweep", start="10Hz", stop="100kHz", points=50, max_duration_s=5)
            assert "estimated" in msg and "max_duration_s" in msg
            assert "settle" in await c.fails("scope_bode_sweep", settle="slow")
            assert "ch2_range" in await c.fails("scope_bode_sweep", ch2_range="-1")
        async with client_for(_sim(sim_factory, caps_clear=int(P.Caps.DECIM)), tmp_path) as c:  # no cap
            assert "decimation filter" in await c.fails("scope_bode_sweep", start="10kHz", stop="100kHz",
                                                        points=3)
        async with client_for(_sim(sim_factory), tmp_path, gen_max_hz=100e3) as c:
            assert "FNIRSI_MCP_GEN_MAX_HZ" in await c.fails("scope_bode_sweep", start="10kHz", stop="1MHz")
    anyio.run(main)
