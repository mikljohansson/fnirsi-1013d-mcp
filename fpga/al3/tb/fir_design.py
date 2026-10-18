#!/usr/bin/env python3
"""Design study for the 0x1535 CIC compensation FIR (fpga/al3, not in the 0x1534 RTL).

Chain (input 200 MSa/s, final decimation N, final output rate fo = 200 MSa/s / N):
  option A: CIC_K decimating by N (as 0x1534), then a T-tap FIR at fo.        (droop only)
  option B: CIC_K decimating by N/2, then a T-tap FIR at 2 fo decimating by 2.  (droop + alias)
The FIR is symmetric (linear phase), designed by weighted least squares (numpy only) on the
CIC response for a large N (the shape converges for N >= 32), quantised to 18-bit signed
coefficients with DC gain 2^17, then evaluated for the actual N.

Usage:
  fir_design.py report            tables for the README / team-lead (both options, T, K)
  fir_design.py coeffs K T [a]    print quantised taps (a = passband edge / (fo/2), default 0.8)
"""
import math
import sys

import numpy as np

FS = 200e6
QBITS = 18                       # signed coefficient width (M18x18)
DC = 1 << (QBITS - 1)            # DC gain of the quantised FIR = sum of taps


def cic_mag(k, m, f):
    """|H| of a K-stage CIC decimating by m at input rate FS, f in Hz (array)."""
    f = np.asarray(f, dtype=float)
    x = np.pi * f / FS
    with np.errstate(divide="ignore", invalid="ignore"):
        r = np.where(np.abs(np.sin(x)) < 1e-15, 1.0, np.sin(m * x) / (m * np.sin(x)))
    return np.abs(r) ** k


def fir_mag(h, f, rate):
    """Zero-phase magnitude of symmetric FIR h at sample rate `rate`, f in Hz."""
    h = np.asarray(h, dtype=float)
    t = len(h)
    c = (t - 1) / 2
    w = 2 * np.pi * np.asarray(f, dtype=float) / rate
    n = np.arange(t) - c
    return np.abs(np.cos(np.outer(w, n)) @ h)


def design(k, t, alpha, option, n_design=4096, stop_w=30.0):
    """Weighted least-squares symmetric FIR. Returns float taps with DC gain 1."""
    m = n_design if option == "A" else n_design // 2
    fo = FS / n_design
    rate = fo if option == "A" else 2 * fo         # FIR input rate
    fp = alpha * fo / 2                            # passband edge
    nyq = rate / 2
    fpass = np.linspace(0, fp, 400)
    rows, target, weight = [], [], []
    half = (t - 1) // 2
    def basis(f):
        w = 2 * np.pi * f / rate
        return np.array([1.0] + [2 * math.cos(w * i) for i in range(1, half + 1)])
    for f in fpass:
        rows.append(basis(f) * cic_mag(k, m, f)); target.append(1.0); weight.append(1.0)
    if option == "B":
        # everything that folds onto [0, fp] after the decimation by 2: [fo - fp, fo] at rate 2 fo
        for f in np.linspace(fo - fp, nyq, 400):
            rows.append(basis(f) * cic_mag(k, m, f)); target.append(0.0); weight.append(stop_w)
    a = np.array(rows) * np.array(weight)[:, None]
    b = np.array(target) * np.array(weight)
    c, *_ = np.linalg.lstsq(a, b, rcond=None)
    h = np.concatenate([c[:0:-1], c])              # symmetric, length 2 * half + 1
    return h / h.sum()


def quantise(h):
    q = np.round(h * DC).astype(np.int64)
    q[len(q) // 2] += DC - q.sum()                 # exact DC gain 2^17
    return q


def evaluate(k, n, hq, alpha, option):
    """Passband ripple, worst alias into [0, fp] (whole chain), in dB."""
    h = hq / DC
    fo = FS / n
    m = n if option == "A" else n // 2
    rate = fo if option == "A" else 2 * fo
    fp = alpha * fo / 2
    fpass = np.linspace(0, fp, 300)
    g = cic_mag(k, m, fpass) * fir_mag(h, fpass, rate)
    ripple = 20 * np.log10(g.max() / g.min())
    droop_cic = 20 * np.log10(cic_mag(k, m, [fp])[0] / cic_mag(k, n, [0])[0]) if True else 0
    # alias: any input frequency in [fo - fp, FS/2] that lands in [0, fp] after the final
    # decimation; worst chain gain over those, relative to the passband gain at DC
    fin = np.linspace(fo - fp, FS / 2, 200000)
    fold = np.abs(((fin + fo / 2) % fo) - fo / 2)
    sel = fin[fold <= fp]
    ga = cic_mag(k, m, sel) * fir_mag(h, sel, rate)
    alias = 20 * np.log10(ga.max())
    return ripple, alias, droop_cic


def cic_only(k, n, alpha):
    fo = FS / n
    fp = alpha * fo / 2
    fpass = np.linspace(0, fp, 300)
    g = cic_mag(k, n, fpass)
    fin = np.linspace(fo - fp, FS / 2, 200000)
    fold = np.abs(((fin + fo / 2) % fo) - fo / 2)
    ga = cic_mag(k, n, fin[fold <= fp])
    return 20 * np.log10(g.max() / g.min()), 20 * np.log10(ga.max())


def report():
    print("Option A = FIR at fo after CIC/N; option B = CIC/(N/2) then FIR/2. a = passband edge / (fo/2).")
    print("Ripple = max/min of the whole chain over [0, a fo/2]. Alias = worst chain gain of any input that")
    print("folds into [0, a fo/2], relative to DC. Coefficients 18-bit, designed at N = 4096.")
    print()
    print("| K | a | option | T | mults/out/ch | ripple N=32 | ripple N=4096 | alias N=32 | alias N=4096 | FIR delay (output samples) |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for k in (1, 2, 3):
        for alpha in (0.5, 0.8):
            r32, a32 = cic_only(k, 32, alpha)
            r4k, a4k = cic_only(k, 4096, alpha)
            print(f"| {k} | {alpha} | CIC only | - | 0 | {r32:.2f} dB | {r4k:.2f} dB | {a32:.1f} dB | {a4k:.1f} dB | 0 |")
            for option, ts in (("A", (7, 15)), ("B", (15, 23, 31))):
                for t in ts:
                    hq = quantise(design(k, t, alpha, option))
                    e32 = evaluate(k, 32, hq, alpha, option)
                    e4k = evaluate(k, 4096, hq, alpha, option)
                    mults = (t + 1) // 2
                    delay = (t - 1) / 2 if option == "A" else (t - 1) / 4
                    print(f"| {k} | {alpha} | {option} | {t} | {mults} | {e32[0]:.2f} dB | {e4k[0]:.2f} dB | "
                          f"{e32[1]:.1f} dB | {e4k[1]:.1f} dB | {delay:g} |")
    return 0


if __name__ == "__main__":
    if len(sys.argv) >= 2 and sys.argv[1] == "report":
        sys.exit(report())
    if len(sys.argv) >= 4 and sys.argv[1] == "coeffs":
        k, t = int(sys.argv[2]), int(sys.argv[3])
        alpha = float(sys.argv[4]) if len(sys.argv) > 4 else 0.8
        print(" ".join(str(int(v)) for v in quantise(design(k, t, alpha, "B"))))
        sys.exit(0)
    print(__doc__)
    sys.exit(2)
