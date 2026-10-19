"""Protocol 1.7 (fw 0.2.28): the spectrum view, READ_SPECTRUM, GET_STATUS display_mode; and the fw 0.2.22-0.2.25
event log additions (bus-level ring, event types 16-19)."""

from __future__ import annotations

import math
import struct

import anyio
import numpy as np
import pytest

from fnirsi1013d import analysis as A
from fnirsi1013d import protocol as P
from fnirsi1013d.scope import Scope
from fnirsi1013d.transport import DeviceError

SINE = "sine:freq=97.65625kHz,amp=0.3"  # bin 20 at 10 MS/s, N = 2048: centred, no leakage


# --- wire format --------------------------------------------------------------------------------------------


def test_spectrum_header_layout():
    h = P.SpectrumHeader(1, 2, 13, 1, 77, 19, 10_000_000, 1_220_703, -4882, 4097, 10, 3, 3770)
    b = h.pack()
    assert len(b) == 32 and P.ReadSpectrumReq.size() == 8
    assert struct.unpack("<BBBBIIIIiHHHH", b) == (1, 2, 13, 1, 77, 19, 10_000_000, 1_220_703, -4882, 4097, 10, 3, 3770)
    back, lv = P.parse_spectrum(P.pack_spectrum(h, [-1348, P.SPECTRUM_EMPTY, 120]))
    assert back == h and lv == (-1348, -32000, 120) and back.window_name == "flattop"
    assert P.ReadSpectrumReq(1, 5, 7).pack() == bytes([1, 0, 5, 0, 7, 0, 0, 0])


def test_status_display_mode_is_byte_43():
    b = P.StatusResp(display_mode=1, ext_flags=4).pack()
    assert len(b) == 48 and b[42] == 4 and b[43] == 1
    assert P.StatusResp.unpack(b[:32]).display_mode == 0  # legacy 32-byte status


def test_event_log_bus_ring_tail():
    """fw 0.2.24: after the reset snapshot, u16 k, u16 0, u32 bus events since boot, k entries."""
    bus = [P.DeviceEvent(900, 1, 0, 0), P.DeviceEvent(901, 13, 0, 64), P.DeviceEvent(901, 17, 0x40, 0x0001)]
    log = P.EventLog(500, 1000, [P.DeviceEvent(990, 5, 0x21, 0xA100)], frozen=False, has_snapshot=True,
                     bus_events=bus, bus_total=42, has_bus_ring=True)
    data = P.pack_event_log(log)
    assert len(data) == 12 + 8 + 8 + 8 + 8 * 3
    back = P.parse_event_log(data)
    assert back == log and back.bus_total == 42 and [e.name for e in back.bus_events][-1] == "tx_abort_txcsr"
    no_ring = P.parse_event_log(data[:12 + 8 + 8])  # fw 0.2.15-0.2.23 end after the snapshot
    assert no_ring.has_snapshot and not no_ring.has_bus_ring and no_ring.bus_events == []


@pytest.mark.parametrize("ev, text", [
    ((16, 0, 0x4005), "usb_init_state POWER 0x40 FADDR 5"),
    ((16, 1, 0x2F03), "usb_init_state INTUSBE 0x2F ccu otg_reset_released,otg_clock_on"),
    ((16, 2, 0x2001), "usb_init_state EP2 TXCSR 0x2001"),
    ((16, 3, 0x0010), "usb_init_state EP2 RXCSR 0x0010"),
    ((16, 4, 0x0123), "usb_init_state ISCR 0x001230 (bits 4-19)"),
    ((17, 0x60, 0x0004), "tx_abort_txcsr POWER 0x60 TXCSR 0x0004 UnderRun (host polls, nothing loaded)"),
    ((17, 0x60, 0x0001), "tx_abort_txcsr POWER 0x60 TXCSR 0x0001 TxPktRdy (packet waits, host takes no IN data)"),
    ((18, 3, 0x0FFF), "tx_abort_frame tx_irq_enabled 1 zlp_pending 1 frame 2047"),
    ((19, 2, 0x0001), "usb_postinit_state ISCR 0x000010 (bits 4-19)"),
    ((19, 3, 0x0303), "usb_postinit_state USBPHY_CFG 0x0303"),
    ((19, 4, 0x00E9), "usb_postinit_state PHY tuning now 0x00E9 (res45_cal 1, tx_amp 0x14, disc_thresh 3, as written)"),
    ((19, 5, 0x0068), "usb_postinit_state PHY tuning after reset 0x0068 (res45_cal 0, tx_amp 0x14, disc_thresh 1)"),
    ((19, 6, 0x0101), "usb_postinit_state PHYCTL 0x0101"),
])
def test_usb_state_event_decoders(ev, text):
    assert P.DeviceEvent(0, *ev).describe() == text


def test_scope_fft_model_level():
    """A sine of A codes centred on a bin reads 20 log10(A / sqrt 2) dB re 1 code on every window."""
    n = 2048
    k = np.arange(n)
    x = 128 + 100 * np.sin(2 * np.pi * 64 * k / n)
    for win in ("rect", "hann", "flattop"):
        db = A.scope_fft_db(x, 128, win, n)
        assert db[64] == pytest.approx(20 * math.log10(100 / math.sqrt(2)), abs=1e-6)
    c = A.compare_spectra(db + 0.05, db)
    assert c["host_peak_bin"] == c["device_peak_bin"] == 64 and c["max_abs_diff_db"] == pytest.approx(0.05)


# --- simulator + client -------------------------------------------------------------------------------------


def test_read_spectrum_needs_the_view(sim_factory):
    srv = sim_factory(signal=SINE)
    with Scope.connect(srv.url) as s:
        assert s.info.proto_minor == P.PROTO_MINOR
        s.capture(["CH1"])
        assert s.status().display_mode == P.DisplayMode.SCOPE
        with pytest.raises(DeviceError) as e:
            s.read_spectrum("CH1")
        assert e.value.status == P.Status.E_STATE
        srv.device.ui_set_spectrum(True, channel=0)
        assert s.status().display_mode == P.DisplayMode.SPECTRUM
        with pytest.raises(DeviceError) as e:
            s.read_spectrum("CH2")  # not selected in the view
        assert e.value.status == P.Status.E_STATE
        with pytest.raises(DeviceError) as e:
            s.t.request(P.Cmd.READ_SPECTRUM, bytes([2]) + bytes(7))
        assert e.value.status == P.Status.E_BAD_ARG


def test_read_spectrum_levels_and_paging(sim_factory):
    srv = sim_factory(signal=SINE)
    srv.device.ui_set_spectrum(True, window=2)
    with Scope.connect(srv.url) as s:
        s.capture(["CH1"])
        sp = s.read_spectrum("CH1")
        h = sp.header
        assert sp.n_fft == 2048 and h.bins == 1025 and sp.cdb.size == 1025 and h.window_name == "flattop"
        assert h.enbw_milli == 3770 and sp.sample_rate == pytest.approx(P.BASE_CLOCK_HZ / (h.divider + 1))
        assert h.bin_width_mhz == int(sp.bin_width * 1000)
        k = int(np.nanargmax(sp.db[1:])) + 1
        assert sp.freqs[k] == pytest.approx(97656.25, abs=sp.bin_width)
        assert sp.db[k] == pytest.approx(20 * math.log10(0.3 / math.sqrt(2)), abs=0.5)  # dBV, firmware gain
        part = s.read_spectrum("CH1", first=k - 2, count=5)
        assert part.header.first == k - 2 and part.header.count == 5 and list(part.cdb) == list(sp.cdb[k - 2: k + 3])
        assert part.header.frame == h.frame  # nothing changed in between
        s.capture(["CH1"])
        assert s.read_spectrum("CH1", 0, 1).header.frame == h.frame + 1  # a new capture: a new spectrum
        tail = s.read_spectrum("CH1", first=5000)
        assert tail.header.count == 0 and tail.cdb.size == 0


def test_read_spectrum_needs_protocol_1_7(legacy_sim):
    with Scope.connect(legacy_sim.url) as s:
        with pytest.raises(DeviceError):
            s.read_spectrum()


# --- MCP tools ----------------------------------------------------------------------------------------------


def test_scope_spectrum_tool(sim_factory, tmp_path):
    from test_server import client_for
    srv = sim_factory(signal=SINE)

    async def main():
        async with client_for(srv, tmp_path) as c:
            await c.call("scope_capture", channels=["CH1"])
            assert "Spectrum analyzer" in await c.fails("scope_spectrum")
            st = await c.call("scope_status")
            assert st["display_mode"] == "scope"
            srv.device.ui_set_spectrum(True)
            st = await c.call("scope_status")
            assert st["display_mode"] == "spectrum"
            r = await c.call("scope_spectrum", channel="CH1", cross_check=True)
            assert r["n_fft"] == 2048 and r["window"] == "hann" and r["rbw_hz"] == pytest.approx(1.5 * r["bin_width"])
            assert r["peaks"][0]["frequency"] == pytest.approx(97656.25, abs=r["bin_width"])
            assert r["peaks"][0]["dbv"] == pytest.approx(-13.5, abs=0.5) and r["file"].endswith(".npz")
            assert len(r["spectrum"]["f"]) == 1025
            x = r["cross_check"]
            assert x["fft"]["max_abs_diff_db"] < 0.05 and x["fft"]["host_peak_bin"] == x["fft"]["device_peak_bin"]
            assert x["volts"]["host_peak_bin"] == x["volts"]["device_peak_bin"]
            assert abs(x["volts"]["median_diff_db"]) < 1.0
            with np.load(r["file"]) as z:
                assert z["f"].size == 1025 and z["level_db"].size == 1025
            cap = await c.call("scope_capture", channels=["CH1"])
            assert any("spectrum view" in cv for cv in cap["caveats"])
            srv.device.ui_set_spectrum(True, channel=0)  # the view shows CH1 only
            assert "no spectrum of CH2" in await c.fails("scope_spectrum", channel="CH2")
            srv.device.ui_set_spectrum(False)
            cap = await c.call("scope_capture", channels=["CH1"])
            assert not any("spectrum view" in cv for cv in cap["caveats"])
    anyio.run(main)


def test_scope_event_log_shows_bus_ring(sim, tmp_path):
    from test_server import client_for
    for _ in range(140):
        sim.device.log_event(5, 0x21, 0xA100)  # EP0 chatter
    sim.device.log_event(13, 0, 64)
    sim.device.log_event(17, 0x40, 0x0001)
    for _ in range(140):
        sim.device.log_event(9, 1)  # DTR edges flood the main ring
    sim.device.log_event(2)

    async def main():
        async with client_for(sim, tmp_path) as c:
            r = await c.call("scope_event_log", last=128)
            assert "tx_aborted" not in [e["type"] for e in r["events"]]
            bus = [e["type"] for e in r["bus_events"]]
            assert bus[:2] == ["tx_aborted", "tx_abort_txcsr"] and "suspend" in bus
            assert r["bus_total_since_boot"] >= 3
    anyio.run(main)


# --- FFT_BENCH (protocol 1.6, fw 0.2.27) --------------------------------------------------------------------


def test_fft_bench_layout_matches_the_firmware_script():
    """firmware/scope/host_tests/fft_bench.py unpacks entries as <HBBIIIIHh after an <HHI head."""
    raw = struct.pack("<HHI", 1, 3, 0x1078) + struct.pack("<HBBIIIIHh", 4096, 1, 1, 340, 7800, 8700, 2000, 205, 3907)
    b = P.parse_fft_bench(raw)
    assert P.FftBenchEntry.size() == 24 and b.reps == 3 and b.cp15 == 0x1078
    e = b.entries[0]
    assert (e.n, e.memory, e.run_min_us, e.peak_bin, e.peak_cdb) == (4096, 1, 7800, 205, 3907)
    assert P.pack_fft_bench(b) == raw


def test_bench_fft_cli(sim_factory, capsys):
    from fnirsi1013d import bench
    srv = sim_factory(signal=SINE)
    with Scope.connect(srv.url) as s:
        s.capture(["CH1"])
        b = s.fft_bench(2)
        assert b.reps == 2 and [(e.n, e.memory) for e in b.entries] == [(1024, 0), (2048, 0), (4096, 0), (16384, 0),
                                                                    (1024, 1), (2048, 1)]
        assert b.entries[1].peak_bin == 20  # the 97.66 kHz tone at 10 MS/s, N = 2048
        with pytest.raises(DeviceError) as e:
            s.t.request(P.Cmd.FFT_BENCH, bytes([17]))
        assert e.value.status == P.Status.E_BAD_ARG
        with pytest.raises(ValueError):
            s.fft_bench(0)
    with pytest.raises(SystemExit) as ex:
        bench.main(["--port", srv.url, "--fft", "--reps", "4"])
    out = capsys.readouterr().out
    assert ex.value.code == 0, out
    assert "FFT_BENCH: 6 entries, reps 4" in out and " 16384   DDR " in out and "(no restart)" in out


# --- protocol 1.13 (fw 0.2.46): display modes 2-4 -----------------------------------------------------------


def test_display_modes_2_to_4(sim_factory, tmp_path):
    from test_server import client_for
    srv = sim_factory(signal=SINE)

    async def main():
        async with client_for(srv, tmp_path) as c:
            await c.call("scope_capture", channels=["CH1"])
            srv.device.ui_set_display(P.DisplayMode.WATERFALL)
            assert (await c.call("scope_status"))["display_mode"] == "waterfall"
            r = await c.call("scope_spectrum", channel="CH1")  # the sub-views show the same spectrum
            assert r["n_fft"] == 2048
            cap = await c.call("scope_capture", channels=["CH1"])
            assert any("spectrum view" in cv for cv in cap["caveats"])
            srv.device.ui_set_display(P.DisplayMode.PHOSPHOR)
            assert (await c.call("scope_status"))["display_mode"] == "phosphor"
            cap = await c.call("scope_capture", channels=["CH1"])
            assert any("phosphor view" in cv for cv in cap["caveats"])
            assert "Spectrum analyzer" in await c.fails("scope_spectrum")
    anyio.run(main)
