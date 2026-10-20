"""Serial decoders (fnirsi1013d.decode) on synthetic lines and the simulator's serial signals, plus the limits they
state (samples per bit)."""

from __future__ import annotations

import time

import numpy as np
import pytest

from fnirsi1013d import decode as D
from fnirsi1013d.simulator import GenState, Signal, companion_signal, parse_signal, serial_timeline

GEN = GenState()
RNG = np.random.default_rng(7)


def line(sig: Signal, fs: float, n: int, t0: float = -20e-6, noise: float = 0.02, step: float = 8.8 / 256
         ) -> tuple[np.ndarray, np.ndarray]:
    """The signal sampled at fs, with noise and 8-bit quantisation (1 V/div: 34 mV per code)."""
    t = t0 + np.arange(n) / fs
    v = sig.eval(t, GEN) + RNG.normal(0, noise, n)
    return t, np.round(v / step) * step


def text_of(frames: list[dict]) -> str:
    return "".join(f.get("char", "?") for f in frames)


# --- logic levels ------------------------------------------------------------------------------------------


def test_to_logic_levels_edges_and_hysteresis():
    v = np.array([0, 0, 0, 3.3, 3.3, 3.3, 0, 0, 1.5, 1.8, 1.5, 0, 0, 3.3, 3.3], float)
    lg = D.to_logic(v)
    assert lg.low == 0 and lg.high == 3.3 and lg.threshold == pytest.approx(1.65)
    assert lg.edges.tolist() == pytest.approx([2.5, 5.5, 12.5])  # the 1.5 .. 1.8 wobble stays inside the band
    assert lg.rising.tolist() == [True, False, True]
    assert lg.level.tolist() == [False] * 3 + [True] * 3 + [False] * 7 + [True] * 2
    with pytest.raises(D.DecodeError, match="no logic swing"):
        D.to_logic(RNG.normal(0, 0.03, 500))
    with pytest.raises(D.DecodeError, match="outside"):
        D.to_logic(v, threshold=5.0)


def test_to_logic_sees_a_single_short_pulse():
    v = np.zeros(16000)
    v[8000:8003] = 3.3
    lg = D.to_logic(v + RNG.normal(0, 0.02, v.size))
    assert lg.edges.size == 2 and lg.rising.tolist() == [True, False]


# --- UART ---------------------------------------------------------------------------------------------------


@pytest.mark.parametrize("parity", ["none", "even", "odd"])
@pytest.mark.parametrize("idle", ["high", "low"])
def test_uart_frames_parity_and_idle(parity, idle):
    pcode = {"none": 0, "even": 1, "odd": 2}[parity]
    sig = parse_signal(f"uart:baud=115200,text=Hi!,parity={pcode},idle={1 if idle == 'high' else 0}")
    t, v = line(sig, 2e6, 1800)  # 0.9 ms: one message and the idle after it
    r = D.decode_uart(t, v, baud=115200, parity=parity, idle=idle)
    assert text_of(r["frames"]) == "Hi!" and r["n_errors"] == 0
    assert r["frames"][0]["t"] == pytest.approx(0.0, abs=1.1 / 2e6)  # the message starts at t = 0
    assert r["applied"]["samples_per_bit"] == pytest.approx(2e6 / 115200)
    assert r["preview"][0].endswith("0x48 'H'")


def test_uart_auto_baud_and_errors():
    sig = parse_signal("uart:baud=9600,text=OK,parity=1,period=5ms")
    t, v = line(sig, 200e3, 560)
    r = D.decode_uart(t, v)  # auto baud, no parity expected: the parity bit reads as a framing error
    assert r["applied"]["baud"] == 9600 and r["applied"]["auto_baud"]
    assert any("auto-baud" in c for c in r["caveats"])
    r = D.decode_uart(t, v, parity="odd")  # the sender used even parity
    assert text_of(r["frames"]) == "OK" and all(f["errors"] == ["parity"] for f in r["frames"])
    assert r["n_errors"] == 2
    r = D.decode_uart(t, v, parity="even")
    assert r["n_errors"] == 0
    # 7 data bits with stop checking: a frame cut by the record end is dropped and said so
    t, v = line(sig, 200e3, 120, t0=0.0)
    r = D.decode_uart(t, v, baud=9600, parity="even")
    assert r["n_frames"] == 0 and any("record ends inside a frame" in c for c in r["caveats"])


def test_uart_sample_rate_limits():
    sig = parse_signal("uart:baud=115200,text=Hi!")
    t, v = line(sig, 115200 * 1.9, 500)
    with pytest.raises(D.DecodeError, match="samples per bit"):
        D.decode_uart(t, v, baud=115200)
    t, v = line(sig, 115200 * 3, 300)
    r = D.decode_uart(t, v, baud=115200)
    assert text_of(r["frames"]) == "Hi!" and any("only 3.0 samples per bit" in c for c in r["caveats"])
    assert D.STANDING and "4 samples per bit" in D.STANDING[1]


def test_uart_decodes_a_full_record_quickly():
    sig = parse_signal("uart:baud=1MHz,text=The quick brown fox jumps over the lazy dog 0123456789,period=4ms")
    t, v = line(sig, 20e6, 16384, t0=-10e-6)
    t0 = time.perf_counter()
    r = D.decode_uart(t, v, baud=1e6)
    assert time.perf_counter() - t0 < 0.5
    assert text_of(r["frames"]) == "The quick brown fox jumps over the lazy dog 0123456789" and r["n_errors"] == 0


# --- SPI ----------------------------------------------------------------------------------------------------


@pytest.mark.parametrize("cpol,cpha", [(0, 0), (0, 1), (1, 0), (1, 1)])
def test_spi_modes(cpol, cpha):
    clk = parse_signal(f"spi_clk:freq=1MHz,text=Hi!,cpol={cpol},cpha={cpha}")
    dat = companion_signal(clk)
    assert dat.kind == "spi_data" and dat.params == clk.params
    t, c = line(clk, 20e6, 1000, t0=-5e-6)
    _, d = line(dat, 20e6, 1000, t0=-5e-6)
    r = D.decode_spi(t, c, d, cpol=cpol, cpha=cpha)
    assert text_of(r["frames"]) == "Hi!" and r["n_errors"] == 0
    assert r["applied"]["clock_hz"] == pytest.approx(1e6, rel=0.02)
    assert r["applied"]["sampling_edge"] == ("rising" if (cpol == 0) == (cpha == 0) else "falling")
    assert all(w["transfer"] == 0 for w in r["frames"])


def test_spi_word_sizes_gaps_and_bit_order():
    clk = parse_signal("spi_clk:freq=2MHz,text=Hi!,bits=12,gap=10us")
    dat = companion_signal(clk)
    t, c = line(clk, 50e6, 4000, t0=-2e-6)
    _, d = line(dat, 50e6, 4000, t0=-2e-6)
    r = D.decode_spi(t, c, d, word_bits=12)
    # "Hi!" = 0x486921 repacked as 12-bit words: 0x486, 0x921
    assert [w["hex"] for w in r["frames"]] == ["0x486", "0x921"] and r["n_errors"] == 0
    assert [w["transfer"] for w in r["frames"]] == [0, 1]  # the 10 us gap separates two transfers
    r8 = D.decode_spi(t, c, d, word_bits=8)
    assert r8["n_errors"] == 2 and all("incomplete" in w["errors"] for w in r8["frames"] if w["bits"] < 8)
    lsb = D.decode_spi(t, c, d, word_bits=12, msb_first=False)
    assert lsb["frames"][0]["value"] == int(f"{0x486:012b}"[::-1], 2)


def test_spi_sample_rate_limit():
    clk = parse_signal("spi_clk:freq=1MHz,text=Hi!")
    dat = companion_signal(clk)
    t, c = line(clk, 1.9e6, 200, t0=-2e-6)
    _, d = line(dat, 1.9e6, 200, t0=-2e-6)
    try:  # under two samples per period the clock aliases: refused, or garbage with the caveat
        r = D.decode_spi(t, c, d)
        assert any("samples per clock period" in x for x in r["caveats"])
    except D.DecodeError as e:
        assert "samples per clock period" in str(e) or "no logic swing" in str(e)
    t, c = line(clk, 3e6, 200, t0=-2e-6)
    _, d = line(dat, 3e6, 200, t0=-2e-6)
    r = D.decode_spi(t, c, d)
    assert any("samples per clock period" in x for x in r["caveats"])


# --- I2C ----------------------------------------------------------------------------------------------------


@pytest.mark.parametrize("rw,nack", [(0, 0), (1, 0), (0, 1)])
def test_i2c_transactions(rw, nack):
    scl = parse_signal(f"i2c_scl:freq=100kHz,addr=80,text=Hi!,rw={rw},nack={nack}")
    sda = companion_signal(scl)
    assert sda.kind == "i2c_sda"
    t, c = line(scl, 2e6, 1200, t0=-20e-6)
    _, d = line(sda, 2e6, 1200, t0=-20e-6)
    r = D.decode_i2c(t, c, d)
    assert r["n_frames"] == 1
    f = r["frames"][0]
    assert f["address"] == "0x50" and f["address_7bit"] == 0x50 and f["rw"] == ("read" if rw else "write")
    assert f["stop"] and not f["repeated_start"]
    if nack:
        assert f["ack"] is False and f["data"] == [] and "NACK on the address" in f["errors"][0]
        assert r["n_errors"] == 1 and r["preview"][0].startswith("-") and "S 0x50 W NACK P" in r["preview"][0]
    else:
        assert f["ack"] is True and [x["value"] for x in f["data"]] == [0x48, 0x69, 0x21]
        acks = [x["ack"] for x in f["data"]]
        assert acks == ([True, True, False] if rw else [True, True, True])  # a read NACKs the last byte
        assert f["errors"] == [] and r["n_errors"] == 0
        assert r["preview"][0].endswith("0x21 NACK P" if rw else "0x21 ACK P")
    assert r["applied"]["scl_hz"] == pytest.approx(100e3, rel=0.03)


def test_i2c_record_cut_inside_a_transaction():
    scl = parse_signal("i2c_scl:freq=100kHz,addr=80,text=Hi!")
    sda = companion_signal(scl)
    # start 120 us into the message: the address and the first byte are gone
    t, c = line(scl, 2e6, 600, t0=120e-6)
    _, d = line(sda, 2e6, 600, t0=120e-6)
    r = D.decode_i2c(t, c, d)
    assert r["n_frames"] == 0 and any("no START" in x for x in r["caveats"])
    # cut before the STOP
    t, c = line(scl, 2e6, 400, t0=-20e-6)
    _, d = line(sda, 2e6, 400, t0=-20e-6)
    r = D.decode_i2c(t, c, d)
    assert r["n_frames"] == 1 and not r["frames"][0]["stop"] and "no STOP" in r["frames"][0]["errors"][-1]


def test_i2c_sample_rate_limits():
    scl = parse_signal("i2c_scl:freq=100kHz,addr=80,text=Hi!")
    sda = companion_signal(scl)
    t, c = line(scl, 500e3, 300)
    _, d = line(sda, 500e3, 300)
    with pytest.raises(D.DecodeError, match="samples per SCL period"):
        D.decode_i2c(t, c, d)
    t, c = line(scl, 700e3, 400)
    _, d = line(sda, 700e3, 400)
    r = D.decode_i2c(t, c, d)
    assert any("samples per SCL period" in x for x in r["caveats"])
    assert r["n_frames"] == 1 and [x["value"] for x in r["frames"][0]["data"]] == [0x48, 0x69, 0x21]


# --- the simulator's serial signals --------------------------------------------------------------------------


def test_serial_timelines_and_dc():
    times, levels, period = serial_timeline("uart", {"baud": 1e6, "text": "U"})
    assert period == 1e-3 and times[0] == 0 and times.size == levels.size
    # 'U' = 0x55: start, then 1 0 1 0 1 0 1 0 (LSB first), stop: alternating levels, 10 bits of 1 us
    assert np.allclose(np.diff(times)[:10], 1e-6) and levels[:10].tolist() == [0, 3.3] * 5
    assert levels[-1] == 3.3  # idle high
    with pytest.raises(ValueError, match="longer than period"):
        serial_timeline("uart", {"baud": 9600, "text": "x" * 20, "period": 1e-3})
    with pytest.raises(ValueError, match="unknown signal kind"):
        parse_signal("spi:freq=1MHz")
    sig = parse_signal("i2c_scl:freq=400kHz,text=ab")
    assert sig.params["text"] == "ab" and sig.params["freq"] == 400e3
    assert 0 < sig.dc < 3.3  # the duty-weighted mean level
    sig = parse_signal("uart:baud=115200,start=100us")
    v = sig.eval(np.array([50e-6, 100.5e-6 + 4.34e-6]), GEN)
    assert v.tolist() == [3.3, 0.0]  # idle before start=, then the start bit
