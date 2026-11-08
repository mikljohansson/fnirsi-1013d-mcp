"""fw-csbusy (bench 2026-11-08, fw 0.3.1 on FPGA 0x153F, CH1 on a 3.3 V 24.576 MHz clock through a x1 probe):
the capture-stage self-check flagged a busy signal (both CH1 cores counted about 43000 outliers, about 2 % apart), and
the counter's freq_mHz field saturated at 4.294967 MHz while a 13-code band read 19.2..20.9 MHz."""

from __future__ import annotations

import anyio

from fnirsi1013d import protocol as P
from fnirsi1013d.scope import Scope
from test_server import client_for


def test_self_check_quiet_partner_rule():
    # Quiet input: a garbled core against a partner at 0 flags as before
    assert P.cap_flagged((0, 0, 40, 0)) == 0x04
    assert P.cap_flagged((0, 0, 8, 0)) == 0 and P.cap_flagged((0, 0, 9, 0)) == 0x04
    # The partner may count up to 8; above that the chip is not checkable (owner 2026-11-08)
    assert P.cap_flagged((17, 8, 0, 0)) == 0x01 and P.cap_flagged((16, 8, 0, 0)) == 0
    assert P.cap_flagged((300, 9, 0, 0)) == 0 and P.cap_busy((300, 9, 0, 0)) == 0x03
    # The bench: a 24.576 MHz clock (both cores near 43000, about 2 % apart) and 354e676's failed run (12.5 MHz at
    # 100 mV/div AC, counts in the hundreds that differ by more than an eighth): busy, not flagged; CH2 still checked
    bench = (42290, 43153, 0, 40)
    assert P.cap_busy(bench) == 0x03 and P.cap_flagged(bench) == 0x08
    assert P.cap_busy((120, 400, 0, 0)) == 0x03 and P.cap_flagged((120, 400, 0, 0)) == 0
    # A core counting thousands against a quiet partner flags; one count each is checkable and clean
    assert P.cap_busy((5000, 3, 0, 0)) == 0 and P.cap_flagged((5000, 3, 0, 0)) == 0x01
    assert P.cap_busy((1, 1, 0, 0)) == 0 and P.cap_flagged((1, 1, 0, 0)) == 0
    assert P.cap_flagged((65535, 65535, 0, 0)) == 0


def _health(flags: int, flagged: int, counts: tuple[int, int, int, int]) -> P.HealthResp:
    return P.HealthResp(layout=3, length=128, cap_value=P.CAP_POWER_ON, cap_flags=flags, cap_flagged=flagged,
                        cap_counts=counts, cap_reads=19060, cap_flagged_n=11087)


def test_describe_capture_busy_and_an_older_firmwares_flag():
    read = P.CAPF_HAS | P.CAPF_TRAINED | P.CAPF_READ
    d = _health(read, 0, (42290, 43153, 0, 0)).describe_capture()
    assert d["busy_cores"] == ["CH1 A", "CH1 B"] and not d["flag_from_busy_signal"]
    assert "last capture too busy to check (CH1)" in d["text"]
    # fw 0.3.1 flagged the busy chip: said so, no advice to calibrate from it
    d = _health(read | P.CAPF_FLAGGED, 0x02, (42290, 43153, 0, 0)).describe_capture()
    assert d["flag_from_busy_signal"] and "not a sign of a garbled capture" in d["text"]
    assert "run the calibration wizard" not in d["text"]
    # A real quiet-input flag stays a flag
    d = _health(read | P.CAPF_FLAGGED, 0x04, (0, 0, 40, 0)).describe_capture()
    assert not d["flag_from_busy_signal"] and d["text"].endswith("CH2 A: 40 outliers; 11087 of 19060 full-rate "
                                                                 "captures flagged since boot")
    assert _health(read, 0, (0, 0, 0, 0)).describe_capture()["text"].endswith("capture clean; 11087 of 19060 "
                                                                             "full-rate captures flagged since boot")
    # 354e676 on the bench: CH1 counts in the hundreds, flagged by its relative rule: an older firmware's busy flag
    d = _health(read | P.CAPF_FLAGGED, 0x02, (120, 400, 0, 0)).describe_capture()
    assert d["flag_from_busy_signal"] and d["busy_cores"] == ["CH1 A", "CH1 B"]


def test_simulator_does_not_flag_a_busy_signal(sim_factory):
    srv = sim_factory()
    d = srv.device
    with Scope.connect(srv.url) as s:
        s.set_timebase(divider=0)
        d.cap_counts_next = [(42290, 43153, 0, 0), (43153, 42290, 0, 0), (0, 0, 0, 41)]
        s.capture(["CH1"])
        s.capture(["CH1"])
        h = s.health()
        assert not h.cap_flags & P.CAPF_FLAGGED and h.cap_flagged == 0 and h.cap_flagged_n == 0
        assert "too busy to check (CH1)" in h.describe_capture()["text"]
        s.capture(["CH1"])
        h = s.health()
        assert h.cap_flagged == 0x08 and h.cap_flagged_n == 1
        assert not [e for e in s.event_log().events if e.type == 23 and e.a & 0x03]


def test_counter_saturated_field():
    r = P.CounterResp(flags=P.COUNTER_F_NEW | P.COUNTER_F_VALID, e=2_457_600, t=20_000_000, w=10_000_000,
                      freq_mhz=P.counter_freq_mhz(2_457_600, 20_000_000))
    assert r.freq_mhz == P.COUNTER_FREQ_MHZ_SAT and r.freq_saturated and r.firmware_frequency_hz is None
    assert r.frequency_hz == 24_576_000
    r = P.CounterResp(flags=P.COUNTER_F_NEW | P.COUNTER_F_VALID, e=1000, t=20_000_000,
                      freq_mhz=P.counter_freq_mhz(1000, 20_000_000))
    assert not r.freq_saturated and r.firmware_frequency_hz == 10_000.0
    assert P.CounterResp().firmware_frequency_hz is None


def test_counter_tool_saturation_and_a_narrow_band(sim_factory, tmp_path):
    big = sim_factory(signal="square:freq=10MHz,amp=1")
    small = sim_factory(signal="square:freq=10MHz,amp=0.08")

    async def main():
        async with client_for(big, tmp_path / "a") as c:
            await c.call("scope_set_channel", channel="CH1", volts_per_div=0.5)
            await c.call("scope_set_timebase", sample_rate="200MS/s")
            r = await c.call("scope_counter")
            row = r["readings"][-1]
            assert r["valid"] and r["frequency_hz"] == 10e6
            assert row["firmware_frequency_hz"] is None and row["firmware_frequency_saturated"]
            assert any("saturates at 4.294967 MHz" in x for x in r["caveats"])
            assert row["max_code"] - row["min_code"] >= P.COUNTER_NARROW_SPAN
            assert not any("spans only" in x for x in r["caveats"])
        async with client_for(small, tmp_path / "b") as c:
            await c.call("scope_set_channel", channel="CH1", volts_per_div=0.5)
            await c.call("scope_set_timebase", sample_rate="200MS/s")
            r = await c.call("scope_counter")
            row = r["readings"][-1]
            span = row["max_code"] - row["min_code"]
            assert r["valid"] and P.COUNTER_AUTO_MIN_SPAN <= span < P.COUNTER_NARROW_SPAN, span
            assert any("spans only" in x and "x10 probe" in x for x in r["caveats"])
    anyio.run(main)
