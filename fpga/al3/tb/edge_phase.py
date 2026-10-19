#!/usr/bin/env python3
"""Edge phase study for the 0x1534 CIC and the 0x1535 CIC + FIR chain (fpga/al3/README.md section 5).

The bench (2026-10-19) showed overshoot "horns" on the 1 kHz calibration square with any +FIR filter that grow and
snap back at about 3 to 4 Hz, while CIC alone is clean, and a spectrum whose upper part "wags" pass to pass. This
script answers three questions with the bit-level model of cic_model.py (boxcar^K CIC with shift and clamp, the
15-tap FIR with its rounding and clamp):

  step   overshoot / undershoot (percent of the step and 8-bit codes) and settling length versus the position of the
         edge on the decimated sample grid, swept in fine sub-sample steps, for CIC alone and CIC + FIR
  beat   the drift of the DDS square's edges against the decimated grid for the firmware's phase increments, and
         the apparent rate seen through the scope's pass rate
  fft    4096-point Hann spectra of the decimated 1 kHz square per edge phase: how much each harmonic's bin
         magnitude moves with the phase, below fo/4 and in the FIR transition band fo/4 .. fo/2

The analog input is the ADC sample stream at 200 MSa/s: an edge through a first-order front end (tau 5.3 ns, about
30 MHz bandwidth, +tau to change), rounded to 8 bits. fo = output rate = 200 MSa/s / N.

Usage: edge_phase.py step | beat | fft [--tau NS]   (needs numpy; run in WSL: python3 -I tb/edge_phase.py step)
"""
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cic_model  # noqa: E402

FS = 200e6
TS = 5.0                                  # ns per ADC sample


def shift_for(k, m):
    return max(0, (255 * m ** k).bit_length() - 16)


class Chain:
    """One decimation setting: K, FIR on/off, divider D (N = 2 (D // 2 + 1))."""

    def __init__(self, k, fir, divider):
        self.k, self.fir = k, fir
        p = divider // 2 + 1
        self.n = 2 * p
        self.nc = p if fir else self.n               # CIC decimation
        self.g = self.nc ** k                        # CIC gain
        self.sh = shift_for(k, self.nc) + (1 if fir else 0)
        self.h = np.array(cic_model.impulse(self.nc, k), dtype=np.int64)
        self.taps = np.array(cic_model.fir_taps(k), dtype=np.int64) if fir else None
        self.tout = self.n * TS                      # ns per output sample

    def name(self):
        return f"{'CIC%d+FIR' % self.k if self.fir else 'CIC%d' % self.k} N={self.n} ({FS / self.n / 1e6:g} MSa/s)"

    def run(self, x, first):
        """x: int ADC codes at 200 MSa/s; first: index of the last input sample of the first CIC output.
        Returns the 16-bit outputs (bit-exact model) and their 8-bit code equivalents."""
        y = np.convolve(x, self.h)[: len(x)]                    # y[i] = sum h[j] x[i - j]
        idx = np.arange(first, len(x), self.nc)
        c = np.minimum(65535, y[idx] >> self.sh)               # CIC samples (16 bit)
        if not self.fir:
            out = c
        else:
            # FIR over 15 consecutive CIC samples, one output per CIC pair (newest of the pair last)
            acc = np.convolve(c, self.taps)[: len(c)]
            acc = acc[14::2]
            out = np.clip((acc + (1 << 16)) >> 17, 0, 65535)
        codes = out * (2.0 ** self.sh) / self.g
        return out, codes


def rc_edge(t, te, lo, hi, tau):
    """ADC codes of an edge at te (ns) through a first-order front end, sampled at t (ns)."""
    v = np.where(t < te, lo, hi - (hi - lo) * np.exp(-np.maximum(t - te, 0) / max(tau, 1e-9)))
    return np.clip(np.rint(v), 0, 255).astype(np.int64)


def step_study(tau, lo, hi, chains, nphase):
    rows = []
    for ch in chains:
        nout = 80
        nin = (nout + 40) * ch.n
        t = np.arange(nin) * TS
        t0 = 40 * ch.n * TS                                   # the edge lands after the filter has settled on lo
        res = []
        for ph in np.linspace(0, ch.tout, nphase, endpoint=False):
            x = rc_edge(t, t0 + ph, lo, hi, tau)
            out, codes = ch.run(x, ch.nc - 1)
            # steady levels from the first and last outputs
            vlo, vhi = codes[5], codes[-1]
            step = vhi - vlo
            k50 = int(np.argmax(codes > vlo + step / 2))
            kp = k50 + int(codes[k50:].argmax())
            over = codes[kp] - vhi
            under_after = max(0.0, vhi - codes[kp:].min())      # ringing below the high level after the peak
            pre = max(0.0, vlo - codes[4:k50].min())             # pre-shoot below the low level (after the warm-up)
            dev = np.abs(codes - vhi)
            bad = np.nonzero(dev[k50:] > 1.0)[0]                 # settled to +/- 1 code
            settle = int(bad[-1]) + 1 if len(bad) else 0
            res.append((ph, over, under_after, pre, settle, step, codes[k50 - 2:k50 + 8]))
        a = np.array([[r[1], r[2], r[3], r[4]] for r in res])
        step = res[0][5]
        imax = int(a[:, 0].argmax()); imin = int(a[:, 0].argmin())
        rows.append((ch, a, step, res[imax][0], res[imin][0]))
        print(f"{ch.name()}: step {step:.1f} codes, shift {ch.sh}, edge swept over {nphase} phases of {ch.tout:g} ns")
        print(f"   overshoot {a[:, 0].min():6.2f} .. {a[:, 0].max():6.2f} codes = {100 * a[:, 0].min() / step:5.2f} .. "
              f"{100 * a[:, 0].max() / step:5.2f} %   (max at phase {res[imax][0]:.0f} ns, min at {res[imin][0]:.0f} ns)")
        print(f"   ringing below the top after the peak {a[:, 1].min():6.2f} .. {a[:, 1].max():6.2f} codes = "
              f"{100 * a[:, 1].min() / step:5.2f} .. {100 * a[:, 1].max() / step:5.2f} %")
        print(f"   pre-shoot below the base {a[:, 2].min():6.2f} .. {a[:, 2].max():6.2f} codes = "
              f"{100 * a[:, 2].min() / step:5.2f} .. {100 * a[:, 2].max() / step:5.2f} %")
        print(f"   settling to +/-1 code after the 50 % sample: {int(a[:, 3].min())} .. {int(a[:, 3].max())} output samples")
        # the overshoot as a function of phase, coarse table
        sel = np.linspace(0, len(res) - 1, 9).astype(int)
        print("   phase/Tout: " + " ".join(f"{res[i][0] / ch.tout:5.2f}" for i in sel))
        print("   overshoot%: " + " ".join(f"{100 * res[i][1] / step:5.2f}" for i in sel))
    return rows


def dds_edges(inc, ticks_ns=20.0, count=4000):
    """Rising edge instants (ns) of the 0x50-0x52 DDS square: output = phase < 2^31, phase += inc per 20 ns."""
    return np.array([math.ceil(k * 2 ** 32 / inc) * ticks_ns for k in range(1, count + 1)])


def beat_study():
    print("DDS 1 kHz: phase increment from fpga_set_generator_freq = floor(f 2^32 / 50 MHz)")
    for f in (1000, 10000, 100000):
        inc = (f * 2 ** 32) // 50_000_000
        fout = inc * 50e6 / 2 ** 32
        T = 1e9 / fout                                         # ns, mean period
        print(f"  f {f} Hz: inc {inc}, actual {fout:.6f} Hz, period {T:.3f} ns")
        for n in (200, 20):
            tout = n * TS
            drift = (T % tout)
            drift = drift if drift < tout / 2 else drift - tout  # signed advance per period on the grid
            rate = abs(drift) / tout * fout                      # grid periods swept per second
            e = dds_edges(inc, count=2000)
            ph = (e % tout) / tout
            print(f"    N {n} (Tout {tout:g} ns): edge advances {drift:+.3f} ns per period on the output grid -> "
                  f"sweeps the grid at {rate:.3f} Hz; DDS 20 ns ticks give {len(set(np.round(ph * tout / 20) % (tout / 20)))} "
                  f"distinct phases")
            for pr in (5, 10, 12, 15, 20, 25, 30):
                a = rate / pr
                al = abs(a - round(a)) * pr                       # apparent rate through the pass rate (aliased)
                print(f"      seen at {pr:2d} passes/s: {al:.2f} Hz", end="")
            print()


def square(t, te, half, lo, hi, tau):
    """Periodic square (period 2 half, rising at te mod 2 half) through the first-order front end."""
    p = np.mod(t - te, 2 * half)
    # periodic steady state: the rising half starts from l0, the falling half from h0
    e = math.exp(-half / tau)
    h0 = hi - (hi - lo) * e / (1 + e)                          # value at the falling edge
    l0 = lo + (hi - lo) * e / (1 + e)                          # value at the rising edge
    v = np.where(p < half, hi - (hi - l0) * np.exp(-np.minimum(p, half) / tau),
                 lo + (h0 - lo) * np.exp(-np.maximum(p - half, 0) / tau))
    return np.clip(np.rint(v), 0, 255).astype(np.int64)


def fft_study(tau, lo, hi, nphase):
    nfft = 4096
    win = np.hanning(nfft)
    # 1 kHz at 1 MSa/s; at 10 MSa/s 4096 samples (410 us) hold less than half a 1 kHz period, so the same
    # harmonic geometry (fo / f = 1000) is taken with a 10 kHz square
    for d, fsq in ((199, 1000.0), (19, 10000.0)):
        for ch in (Chain(2, False, d), Chain(2, True, d)):
            fo = FS / ch.n
            half = 0.5e9 / fsq                                   # ns
            nin = (nfft + 60) * ch.n
            t = np.arange(nin) * TS
            mags = []
            for ph in np.linspace(0, ch.tout, nphase, endpoint=False):
                x = square(t, 1000.0 + ph, half, lo, hi, tau)
                _, codes = ch.run(x, ch.nc - 1)
                seg = codes[40:40 + nfft]
                spec = np.abs(np.fft.rfft((seg - seg.mean()) * win)) / (win.sum() / 2)
                mags.append(spec)
            mags = np.array(mags)
            freqs = np.fft.rfftfreq(nfft, 1 / fo)
            # odd harmonics of 1 kHz: magnitude = the largest of the 3 nearest bins (bin spacing fo / 4096)
            res = {"low": [], "band": [], "top": []}
            for hnum in range(1, int(fo / 2 / fsq), 2):
                fh = fsq * hnum
                b = int(round(fh / (fo / nfft)))
                if b < 2 or b + 2 >= len(freqs):
                    continue
                m = mags[:, b - 1:b + 2].max(axis=1)
                db = 20 * np.log10(np.maximum(m, 1e-12))
                reg = "low" if fh < fo / 4 else ("band" if fh < 3 * fo / 8 else "top")
                res[reg].append((fh, db.max() - db.min(), db.mean()))
            print(f"{ch.name()}: {fsq / 1e3:g} kHz square, {lo}..{hi} codes, {nphase} edge phases over one output period")
            for reg, label in (("low", f"below fo/4 ({fo / 4e3:g} kHz)"),
                               ("band", f"fo/4 .. 3fo/8 ({fo / 4e3:g} .. {3 * fo / 8e3:g} kHz)"),
                               ("top", f"3fo/8 .. fo/2 ({3 * fo / 8e3:g} .. {fo / 2e3:g} kHz)")):
                r = res[reg]
                if not r:
                    continue
                v = np.array([x[1] for x in r])
                lv = np.array([x[2] for x in r])
                worst = r[int(v.argmax())]
                print(f"   {label}: {len(r)} harmonics, bin magnitude spread over the phases: median {np.median(v):.2f} dB, "
                      f"90th pct {np.percentile(v, 90):.2f} dB, max {v.max():.2f} dB (at {worst[0] / 1e3:g} kHz, mean level "
                      f"{worst[2]:.1f} dB codes); mean level of the region {lv.mean():.1f} dB codes")


def main():
    tau = 5.3
    if "--tau" in sys.argv:
        tau = float(sys.argv[sys.argv.index("--tau") + 1])
    what = sys.argv[1] if len(sys.argv) > 1 else ""
    if what == "step":
        chains = []
        for d in (199, 19):
            for k in (1, 2, 3):
                chains += [Chain(k, False, d), Chain(k, True, d)]
        print(f"Front end tau {tau} ns. Full-scale step 0 -> 255 (the FIR output clamps at 0 below the base):")
        step_study(tau, 0, 255, [c for c in chains if c.k == 2], 200)
        print()
        print(f"Mid-scale step 64 -> 192 (no clamping):")
        step_study(tau, 64, 192, chains, 200)
    elif what == "beat":
        beat_study()
    elif what == "fft":
        print(f"Front end tau {tau} ns")
        fft_study(tau, 64, 192, 24)
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
