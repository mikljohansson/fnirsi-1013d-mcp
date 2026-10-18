#!/usr/bin/env python3
"""Alias bin for a DDS square (bench checks of README sections 3 and 5). Default: the 100 kHz bin for a ~900 kHz square at 1 MSa/s (N = 200), CIC K = 0..3 and the 0x1535 FIR.
(a) ideal square, all odd harmonics, analytic (CIC response, folded);
(b) the FPGA DDS square (cmd 0x51 phase increment at 50 MHz, edges on the 20 ns grid), simulated at
    200 MSa/s through the filter models, FFT of the decimated output.
Usage: square_alias.py                        900 kHz and 50 MHz / 56 at N = 200
       square_alias.py N label M f_lo f_hi    square at 50 MHz / M, peak searched in [f_lo, f_hi] Hz
Levels: 3.1 V p-p square (measured cal-post swing -1.23 / +1.87 V), dBV rms; front end ignored."""
import sys
import numpy as np
import os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fir_design as fd

FS = 200e6
VPP = 3.1
N = 200

def cic_mag(k, m, f):
    x = np.pi * np.asarray(f, float) / FS
    s = np.sin(x)
    r = np.where(np.abs(s) < 1e-15, 1.0, np.sin(m * x) / (m * np.where(np.abs(s) < 1e-15, 1, s)))
    return np.abs(r) ** k

def dbv(a_peak):
    return 20 * np.log10(a_peak / np.sqrt(2) + 1e-30)

def ideal(f0, k):
    fo = FS / N
    tot = 0.0
    contrib = []
    for h in range(1, 4001, 2):
        f = h * f0
        if f > 100e6:          # the ADC itself would fold above 100 MHz; front end has ~30 MHz bandwidth anyway
            break
        fold = abs(((f + fo / 2) % fo) - fo / 2)
        if abs(fold - abs(fo - f0 % fo if f0 % fo > fo / 2 else f0 % fo)) < 1.0:
            a = 4 / np.pi * VPP / 2 / h * (cic_mag(k, N, f) if k else 1.0)
            contrib.append((h, a))
            tot += a * a
    return np.sqrt(tot), contrib

def dds_wave(inc, nsamp):
    n = np.arange(nsamp, dtype=np.int64)
    tick = n // 4                                    # 50 MHz DDS clock, 4 ADC samples per tick
    phase = (tick * inc) & 0xFFFFFFFF
    return np.where(phase < (1 << 31), VPP / 2, -VPP / 2)

def cic_chain(x, k, m):
    y = x.astype(float)
    for _ in range(k):
        c = np.concatenate(([0.0], np.cumsum(y)))
        y = (c[m:] - c[:-m]) / m
    return y

def filt(x, k, fir):
    if k == 0:
        return x[::N]
    if not fir:
        return cic_chain(x, k, N)[N - 1::N]
    c = cic_chain(x, k, N // 2)[N // 2 - 1::N // 2]
    h = fd.quantise(fd.design(k, 15, 0.5, "B")) / fd.DC
    return np.convolve(c, h, mode="valid")[::2]

def spectrum_peak(y, f_lo, f_hi, rate):
    m = 1 << 13
    y = y[-m:] - np.mean(y[-m:])
    w = np.hanning(m)
    sp = np.abs(np.fft.rfft(y * w)) / (w.sum() / 2)
    fr = np.fft.rfftfreq(m, 1 / rate)
    sel = (fr >= f_lo) & (fr <= f_hi)
    i = np.argmax(np.where(sel, sp, 0))
    return fr[i], sp[i]

def run(label, inc, lo=90e3, hi=110e3):
    f0 = inc * 50e6 / 2 ** 32
    print(f"{label}: inc {inc}, f = {f0:.3f} Hz, fundamental {dbv(4/np.pi*VPP/2):.1f} dBV")
    x = dds_wave(inc, (1 << 13) * N + 40 * N)
    for k, fir in ((0, 0), (1, 0), (2, 0), (3, 0), (1, 1), (2, 1), (3, 1)):
        ia, contrib = ideal(f0, k) if not fir else (None, None)
        y = filt(x, k, fir)
        fr, a = spectrum_peak(y, lo, hi, FS / N)
        s = f"  K {k} {'FIR' if fir else 'CIC' if k else 'skip'}: DDS sim {dbv(a):6.1f} dBV at {fr/1e3:.1f} kHz"
        if ia is not None:
            top = ", ".join(f"h{h} {dbv(v):.0f}" for h, v in sorted(contrib, key=lambda t: -t[1])[:3])
            s += f" | ideal square {dbv(ia):6.1f} dBV ({top})"
        print(s)

if len(sys.argv) > 1:
    N = int(sys.argv[1]); run(sys.argv[2], round(2 ** 32 / int(sys.argv[3])), float(sys.argv[4]), float(sys.argv[5])); sys.exit(0)
run("DDS 900 kHz", round(900e3 * 2 ** 32 / 50e6))
run("DDS 50 MHz / 56 = 892.857 kHz (integer period)", round(2 ** 32 / 56))
