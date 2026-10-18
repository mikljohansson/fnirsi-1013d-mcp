"""The server as a real subprocess over stdio (what Claude Code runs).

Regression test: on Windows, importing scipy's BLAS DLL from the event loop deadlocked against the stdio reader
thread's pending ReadFile on stdin; the in-memory transport used by test_server.py cannot see that.
"""

from __future__ import annotations

import os
import sys

import anyio
import pytest
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

pytestmark = pytest.mark.anyio


@pytest.fixture
def anyio_backend():
    return "asyncio"


async def test_server_subprocess_decay_fit(sim_factory, tmp_path):
    srv = sim_factory(signal="decay:tau=80us,amp=0.3")
    env = dict(os.environ, FNIRSI_MCP_DATA_DIR=str(tmp_path / "data"), FNIRSI_MCP_AUDIT_LOG=str(tmp_path / "a.jsonl"))
    params = StdioServerParameters(command=sys.executable, args=["-m", "fnirsi1013d.server", "--port", srv.url],
                                   env=env)
    with anyio.fail_after(90):
        with open(tmp_path / "server.err", "w") as err:
            async with stdio_client(params, errlog=err) as (r, w):
                async with ClientSession(r, w) as s:
                    await s.initialize()
                    names = {t.name for t in (await s.list_tools()).tools}
                    assert "scope_decay_fit" in names and "scope_raw_fpga" not in names  # default tier 3

                    async def call(name, **args):
                        res = await s.call_tool(name, args)
                        assert not res.isError, res.content
                        return res.structuredContent

                    await call("scope_set_channel", channel="CH1", volts_per_div=0.1)
                    await call("scope_set_channel", channel="CH2", volts_per_div=2.5)
                    await call("scope_set_timebase", sample_rate="10MS/s")
                    await call("scope_set_trigger", source="CH2", edge="falling", level=1.65, mode="normal",
                               pretrigger_fraction=0.05, total_samples=16000)
                    r = await call("scope_decay_fit", edge_channel="CH2", t_start="5us", t_end="400us", n_avg=8)
                    assert abs(r["tau"] - 80e-6) / 80e-6 < 0.03
                    assert r["restarts"] == []
                    st = await call("scope_status")
                    assert st["control"] == "SHARED" and st["run_state"] == "STOP"
