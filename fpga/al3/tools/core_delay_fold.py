"""Core delay at every divider by equivalent-time folding: the generator at 200 MHz / P, P prime, so a core's samples cover
every phase of the period at any divider. Time in 5 ns clocks: B[k] = f(2 (D + 1) k), A[k] = f(2 (D + 1) k + d (D + 1)). d in
record samples: +1 designed, +3 = B one conversion late (0x153E's fault at most rates).

    python core_delay_fold.py 0,1,2,3,4,5,6,7,9,19,99,999 8

Both probes on the calibration post itself: any periodic waveform works (only both cores seeing the same f matters), but the
ladder's rounding makes the minimum shallow. Better than core_order.py at divider 0, whose moving-word metric carry errors
take over when a level sits at a carry. An alias: at divider 999 d = +3 reads low too (2000 clocks = -5 mod 401). It sets
the generator and puts it back to 1 kHz."""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "mcp", "src"))
from fnirsi1013d.scope import Scope  # noqa: E402
from fnirsi1013d.transport import Transport, open_link  # noqa: E402

DIVS = [int(x) for x in sys.argv[1].split(",")]; N = int(sys.argv[2]); P = 401; DS = list(range(-3, 6))
def fold(x, t):
    ph = t % P; return np.bincount(ph, weights=x, minlength=P), np.bincount(ph, minlength=P)
def score(A, B, d, D):
    k = np.arange(len(A)) * 2 * (D + 1)
    sb, nb = fold(B - np.mean(B), k); sa, na = fold(A - np.mean(A), k + d * (D + 1))
    ok = (nb > 0) & (na > 0); r = sb[ok] / nb[ok] - sa[ok] / na[ok]
    return float(np.sqrt(np.mean((r - r.mean()) ** 2)))
s = Scope(Transport(open_link("COM10")))
try:
    tb0 = s.get_timebase(cached=False); s.set_decim(0); s.set_gen(enable=True, freq_hz=200e6 / P, duty=0.5)
    s.set_trigger(source=0, edge=0, level=128, mode=0, pretrigger_samples=1500, total_samples=3000)
    for D in DIVS:
        s.set_timebase(divider=D); acc = {"CH1": [], "CH2": []}; sw = {}
        for i in range(N):
            c = s.capture(["CH1", "CH2"], 2.0, keep_raw=True)
            if c is None: continue
            for n in acc:
                ch = c.channels[n]; A = np.asarray(ch.raw_a, float); B = np.asarray(ch.raw_b, float)
                sw[n] = int(A.max() - A.min()); acc[n].append([score(A, B, d, D) for d in DS])
        out = []
        for n, v in acc.items():
            v = np.array(v); m = v.mean(axis=0); w = v.argmin(axis=1); vt = {DS[i]: int((w == i).sum()) for i in set(w)}
            out.append(f"{n} best {DS[int(m.argmin())]:+d} votes {dict(sorted(vt.items()))} RMS d0/+1/+2/+3 {m[3]:.1f}/{m[4]:.1f}/{m[5]:.1f}/{m[6]:.1f} swing {sw[n]}")
        print(f"divider {D:5d}: " + " | ".join(out), flush=True)
    h = s.health(); print("stages 0x%04X flagged_n %d enc_bad %d" % (h.cap_value, h.cap_flagged_n, h.enc_bad))
finally:
    try:
        s.set_gen(enable=True, freq_hz=1000.0, duty=0.5); s.set_timebase(divider=tb0.divider)
        s.set_trigger(source=0, edge=0, level=137, mode=1, pretrigger_samples=1500, total_samples=3000)
    finally: s.close()
