"""Shared fixtures: an in-process simulator on an ephemeral TCP port and clients for it."""

from __future__ import annotations

import pytest

from fnirsi1013d.scope import Scope
from fnirsi1013d.simulator import SimConfig, parse_signal, start_simulator
from fnirsi1013d.transport import connect


def make_cfg(**kw) -> SimConfig:
    kw.setdefault("time_scale", 0.0)
    kw.setdefault("seed", 1234)
    if not kw.get("legacy"):
        # collaborative mode: UI RUN captures only on demand (Device.ui_run_capture), so tests are deterministic
        kw.setdefault("ui_capture_period_s", 0.0)
    sig = kw.pop("signal", None)
    if sig is not None:
        from fnirsi1013d.simulator import companion_signal
        s1 = parse_signal(sig)
        kw["signals"] = (s1, companion_signal(s1))
    return SimConfig(**kw)


@pytest.fixture(autouse=True)
def _user_home(tmp_path_factory, monkeypatch):
    """Nothing a test runs may write into the real per-user home (~/.fnirsi1013d)."""
    monkeypatch.setenv("FNIRSI_MCP_HOME", str(tmp_path_factory.mktemp("home")))


@pytest.fixture
def sim_factory():
    servers = []

    def make(**kw):
        srv = start_simulator(make_cfg(**kw))
        servers.append(srv)
        return srv

    yield make
    for s in servers:
        s.stop()


@pytest.fixture
def sim(sim_factory):
    return sim_factory()


@pytest.fixture
def legacy_sim(sim_factory):
    """Firmware without collaborative mode (protocol 1.2, exclusive LOCAL/REMOTE)."""
    return sim_factory(legacy=True)


@pytest.fixture
def legacy_transport(legacy_sim):
    t = connect(legacy_sim.url)
    yield t
    t.close()


@pytest.fixture
def transport(sim):
    t = connect(sim.url)
    yield t
    t.close()


@pytest.fixture
def scope(sim):
    s = Scope.connect(sim.url)
    yield s
    s.close()
