"""GET_MEASURES / SET_MEASURES (protocol 1.32, fw 0.4.1, docs/protocol.md row 75): the Scope's measurement cards."""

import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d.scope import DeviceError, Scope, ScopeError


def test_measures_roundtrip(scope, sim):
    m = scope.get_measures()
    assert m.names(0) == ["Freq", "Vpp", "Vavg"] and m.names(1) == ["Freq", "Vpp", "Vavg"] and m.flags == 0
    seq = scope.status().settings_seq
    m = scope.set_measures(ch1=["+duty", "Freq", "Vrms", "Vpp"], expanded=True)
    assert m.names(0) == ["+Duty", "Freq", "Vrms", "Vpp"]
    assert m.names(1) == ["Freq", "Vpp", "Vavg"]            # not in the mask: as it was
    assert m.flags == P.MEAS_F_EXPANDED and m.mask == 0
    m = scope.set_measures(ch2=[], hide_ch1=True)
    assert m.names(1) == [] and m.flags == P.MEAS_F_EXPANDED | P.MEAS_F_HIDE_CH1
    assert scope.get_measures().names(0) == ["+Duty", "Freq", "Vrms", "Vpp"]
    assert scope.status().settings_seq == seq               # the screen only, not a capture setting


def test_measures_names_checked():
    assert P.Measures.ranks(["Vpp", "FREQ"]) == [0, 0, 0, 0, 1, 0, 2, 0, 0, 0, 0, 0]
    with pytest.raises(ValueError, match="unknown measurement"):
        P.Measures.ranks(["Vpk"])
    with pytest.raises(ValueError, match="twice"):
        P.Measures.ranks(["Vpp", "vpp"])
    assert P.Measures.size() == 28


def test_measures_bad_arguments(scope, sim):
    bad = bytearray(P.Measures(mask=1).pack())
    bad[4] = 13
    with pytest.raises(DeviceError) as e:
        scope._req(P.Cmd.SET_MEASURES, bytes(bad))
    assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, 4)
    with pytest.raises(DeviceError) as e:
        scope._req(P.Cmd.SET_MEASURES, P.Measures(mask=8).pack())
    assert (e.value.status, e.value.detail) == (P.Status.E_BAD_ARG, 2)
    assert scope.get_measures().names(0) == ["Freq", "Vpp", "Vavg"]


def test_measures_need_protocol_1_32(sim_factory):
    srv = sim_factory(proto_minor=31)
    s = Scope.connect(srv.url)
    try:
        assert not s.has_measures
        with pytest.raises(ScopeError, match="protocol 1.32"):
            s.get_measures()
    finally:
        s.close()
