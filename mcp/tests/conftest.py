"""Shared fixtures: an in-process simulator on an ephemeral TCP port and clients for it."""

from __future__ import annotations

import pytest

from fnirsi1013d.scope import Scope
from fnirsi1013d.simulator import SimConfig, parse_signal, start_simulator
from fnirsi1013d.transport import connect


def make_cfg(**kw) -> SimConfig:
    kw.setdefault("time_scale", 0.0)
    kw.setdefault("seed", 1234)
    sig = kw.pop("signal", None)
    if sig is not None:
        from fnirsi1013d.simulator import companion_signal
        s1 = parse_signal(sig)
        kw["signals"] = (s1, companion_signal(s1))
    return SimConfig(**kw)


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
def transport(sim):
    t = connect(sim.url)
    yield t
    t.close()


@pytest.fixture
def scope(sim):
    s = Scope.connect(sim.url)
    yield s
    s.close()
