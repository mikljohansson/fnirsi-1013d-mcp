"""Core delay at 200 MSa/s by folding: the generator at 200 MHz / 201 (period 201 samples, odd, so a core's even-spaced
samples cover every phase). Model B[k] = f(2k), A[k] = f(2k + d). Per capture fold both cores on the period for each d and
take the RMS of FB - FA (means removed). d = +1 designed (A one sample after B), d = +3 = B one conversion late (the
0x153E fault at A3 B3), d = -1 = B one early. Prints mean RMS per d and the votes. Use this, not core_order.py, at divider
0: there core_order.py's moving-word metric is taken over by carry errors when a level sits at a carry (2026-11-07, CH2).
Any periodic waveform works (the ladder rounds the square; only both cores seeing the same f matters), but the sharper
the waveform the deeper the minimum: the probes on the post itself separate d best. It sets the generator and puts it
back to 1 kHz."""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "mcp", "src"))
from fnirsi1013d.scope import Scope  # noqa: E402
from fnirsi1013d.transport import Transport, open_link  # noqa: E402

N = int(sys.argv[1]) if len(sys.argv) > 1 else 30
P = 201; DS = list(range(-5, 8))
def fold(x, t):
    ph = t % P; s = np.bincount(ph, weights=x, minlength=P); n = np.bincount(ph, minlength=P)
    return s, n
def score(A, B, d):
    k = np.arange(len(A)) * 2
    sb, nb = fold(B - np.median(B), k); sa, na = fold(A - np.median(A), k + d)
    ok = (nb > 0) & (na > 0)
    fb = sb[ok] / nb[ok]; fa = sa[ok] / na[ok]
    return float(np.sqrt(np.mean((fb - fb.mean() - fa + fa.mean()) ** 2)))
s = Scope(Transport(open_link("COM10")))
g0 = s.get_gen()
try:
    tb0 = s.get_timebase(cached=False); s.set_decim(0)
    s.set_gen(enable=True, freq_hz=200e6 / P, duty=0.5)
    s.set_timebase(divider=0)
    s.set_trigger(source=0, edge=0, level=128, mode=0, pretrigger_samples=1500, total_samples=3000)
    acc = {c: [] for c in ("CH1", "CH2")}; swing = {}
    for i in range(N):
        c = s.capture(["CH1", "CH2"], 2.0, keep_raw=True)
        if c is None: continue
        for name in acc:
            ch = c.channels[name]; A = np.asarray(ch.raw_a, float); B = np.asarray(ch.raw_b, float)
            swing[name] = (A.min(), A.max(), B.min(), B.max())
            acc[name].append([score(A, B, d) for d in DS])
    h = s.health(); print("stages 0x%04X flagged_n %d" % (h.cap_value, h.cap_flagged_n))
    for name, v in acc.items():
        v = np.array(v); m = v.mean(axis=0); w = v.argmin(axis=1)
        votes = {DS[i]: int((w == i).sum()) for i in set(w)}
        print(f"{name}: {len(v)} captures, swing A/B {swing[name]}, best d {DS[int(m.argmin())]}, votes {dict(sorted(votes.items()))}")
        print("   RMS per d: " + " ".join(f"{d:+d}:{x:.2f}" for d, x in zip(DS, m)))
finally:
    try:
        s.set_gen(enable=True, freq_hz=1000.0, duty=0.5); s.set_timebase(divider=tb0.divider)
        s.set_trigger(source=0, edge=0, level=137, mode=1, pretrigger_samples=1500, total_samples=3000)
    finally: s.close()
