"""Core order per divider on the unit (fpga/al3 bench tool; docs/plans/core-order-gated.md, bench log 2026-11-06).

    python core_order.py [dividers] [captures per divider] [channel] [port]
    python core_order.py 0,1,2,3,4,5,6,7,9,19,999 12 CH1 COM10

Filter Off, raw per-core records. The channel needs an edge with a slope of a few codes per word at the rates under
test (the bench used CH1 on the RC ladder's input, driven by the calibration post's 1 kHz square: a step of about 40
codes in 200 ns on each rising edge); the trigger is set to that channel's rising edge at code 115 and put back to code
137 afterwards. Per capture, on the words where core A moves 4..70 codes: the RMS of B[k+s] - (A[k-1] + A[k]) / 2 (the
mean A - B offset removed) for s = -1..2. s = 0 is the designed order (B half a word before A within a ring word),
s = +1 = core B one conversion late. Prints the winner per capture as votes and the mean RMS per s.

Model that fitted the unit on 0x153E (AD9288 in data-align mode: both cores' data change D = 12 ns after the write
clock's rising edge R; core B taken at R - cB, core A at R + T/2 - cA, c = 5 + 2.5 x stage ns, T = 10 (divider + 1) ns):
s = 1 + (ceil((cB + D) / T) - 1) - (ceil((cA + T/2 + D) / T) - 1). On 0x153F (both cores of a chip taken at one
instant, fpga/al3/README.md section 22) every divider must read s = 0, at the trained stages and after a retrain.
Run with the mcp venv's Python; LOCAL-safe except
that it sets the sample rate, Filter Off and the trigger (it restores the rate and a mid-level trigger)."""
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "mcp", "src"))
from fnirsi1013d.scope import Scope  # noqa: E402
from fnirsi1013d.transport import Transport, open_link  # noqa: E402

DIVS = [int(x) for x in sys.argv[1].split(",")] if len(sys.argv) > 1 else [0, 1, 2, 3, 4, 5, 6, 7, 9, 19, 999]
N = int(sys.argv[2]) if len(sys.argv) > 2 else 12
CH = sys.argv[3] if len(sys.argv) > 3 else "CH1"
PORT = sys.argv[4] if len(sys.argv) > 4 else "COM10"


def mid(A, B, lo=4, hi=70):
    A = A.astype(float)
    B = B.astype(float)
    out = {}
    dA = np.abs(np.diff(A)); idx = np.where((dA >= lo) & (dA <= hi))[0] + 1      # word k where A[k] - A[k-1] moves
    idx = idx[(idx >= 3) & (idx < len(A) - 3)]
    if len(idx) < 2: return None, len(idx)
    for s in (-1, 0, 1, 2):
        r = B[idx + s] - (A[idx - 1] + A[idx]) / 2
        out[s] = float(np.sqrt(np.mean((r - np.mean(B - A)) ** 2)))
    return out, len(idx)
s = Scope(Transport(open_link(PORT)))
try:
    st = s.status; st = st() if callable(st) else st
    h = s.health(); print("ui_busy", st.ui_busy, "stages 0x%04X" % h.cap_value, "flagged_n", h.cap_flagged_n)
    tb0 = s.get_timebase(cached=False)
    s.set_decim(0)
    tr = s.get_trigger(cached=False)
    s.set_trigger(source=0, edge=0, level=115, mode=1, pretrigger_samples=1500, total_samples=3000)
    for d in DIVS:
        s.set_timebase(divider=d)
        votes = {}; acc = {k: [] for k in (-1, 0, 1, 2)}; nw = 0
        for i in range(N):
            c = s.capture([CH], 3.0, keep_raw=True)
            if c is None: continue
            ch = c.channels[CH]
            m, n = mid(np.asarray(ch.raw_a), np.asarray(ch.raw_b))
            if m is None: continue
            nw += n; w = min(m, key=m.get); votes[w] = votes.get(w, 0) + 1
            for k in m: acc[k].append(m[k])
        if not votes: print(f"divider {d:5d} ({200e6/(d+1)/1e6:8.3f} MSa/s): no usable words"); continue
        print(f"divider {d:5d} ({200e6/(d+1)/1e6:8.3f} MSa/s): winner votes {dict(sorted(votes.items()))}  mean RMS s=-1/0/1/2: " + " / ".join(f"{np.mean(acc[k]):.2f}" for k in (-1, 0, 1, 2)) + f"  ({nw} words)")
finally:
    try:
        s.set_timebase(divider=tb0.divider)
        s.set_trigger(source=0, edge=0, level=137, mode=1, pretrigger_samples=1500, total_samples=3000)
    finally: s.close()
