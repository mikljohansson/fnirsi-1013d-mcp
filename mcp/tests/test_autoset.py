"""AUTO_SET (protocol 1.31, fw 0.3.5, docs/protocol.md row 74): the Scope view's AUTO SET from the host."""

import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d.scope import DeviceError, Scope, ScopeError


def test_auto_set_lands_on_the_ladder(scope, sim):
    """Issue #2: a 10 MHz signal gave 20 ns/div, which the zoom cannot return to; now 50 ns/div at 200 MSa/s."""
    scope.set_gen(enable=True, freq_hz=10e6, duty=0.5)
    seq = scope.status().settings_seq
    scope.auto_set()
    assert scope.status().settings_seq != seq
    tb = scope.get_timebase(cached=False)
    assert (tb.timeperdiv_index, tb.divider) == (31, 0)
    # 1 kHz: five divisions a period (200 us/div) at the fastest rate that still covers the screen
    scope.set_gen(enable=True, freq_hz=1e3, duty=0.5)
    scope.auto_set()
    tb = scope.get_timebase(cached=False)
    assert P.TIME_PER_DIV[tb.timeperdiv_index] == pytest.approx(200e-6)
    assert tb.divider == P.SAMPLE_RATE_DIVIDERS[tb.samplerate_index] == 199


def test_auto_set_refused_in_a_spectrum_view(scope, sim):
    sim.device.ui_set_spectrum(True)
    with pytest.raises(DeviceError) as e:
        scope.auto_set()
    assert (e.value.status, e.value.detail) == (P.Status.E_STATE, P.AUTOSET_E_VIEW)
    sim.device.ui_set_spectrum(False)
    scope.auto_set()


def test_auto_set_needs_protocol_1_31(sim_factory):
    srv = sim_factory(proto_minor=30)
    s = Scope.connect(srv.url)
    try:
        assert not s.has_auto_set
        with pytest.raises(ScopeError, match="protocol 1.31"):
            s.auto_set()
    finally:
        s.close()
