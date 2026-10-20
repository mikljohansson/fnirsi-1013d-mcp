"""Raw records of single Bode cells (M, N) for comparing the device engine with the host tool (fw-eng2, 2026-10-20).

    python -I firmware/tools/bode_cells.py COM10 backups/bode/cells_<hhmm>.npz

Bench set-up: the RC 1 kOhm / 15 nF on the post, wiring A, x1 probes, CH1 on the post, CH2 on the node; Frequency
response view closed (Menu > Scope); the MCP server's port closed.

Per cell (the bench sweep's 100 Hz .. 1 MHz / 25 plan: 10016.03, 14671.36 (control), 21551.72, 31625.55 Hz) it sets the
host tool's way (CIC3 + FIR, divider N - 1, both channels DC on 0.5 V/div offset 0, the DDS at the plan's increment)
and takes captures in three trigger variants, eight each:
  host   CH1 rising at code 128, auto, pretrigger 0 (bode.py's set-up), settle as the host (10 periods + 40 ms)
  imm    the same, the arm right after the generator write (no settle: the device's shortest path)
  auto   level 255 (never crosses): the FPGA auto time-out starts the record, as the device's capture often does
Saved per capture: the 16-bit filter outputs of both channels (samples16), the codes, volts per code, the plan and the
variant. The analysis (fw-eng2) looks for a filter warm-up at the record start and runs the C DFT on the same records.
"""
import sys
import time

import numpy as np

from fnirsi1013d import bode as B
from fnirsi1013d.scope import Scope

PORT = sys.argv[1] if len(sys.argv) > 1 else "COM10"
OUT = sys.argv[2] if len(sys.argv) > 2 else "bode_cells.npz"
CELLS = (10016.03, 14671.36, 21551.72, 31625.55)
REPEATS = 8

plans = {round(pp.f_actual, 2): pp for pp in B.plan_sweep(100.0, 1e6, 25)}
data = {}
with Scope.connect(PORT) as sc:
    print("fw", sc.identify().fw_version.rstrip(b"\0"))
    saved_ch = [sc.get_channel(c, cached=False) for c in (0, 1)]
    saved_tb = sc.get_timebase() if hasattr(sc, "get_timebase") else None
    saved_tr = sc.get_trigger()
    sc.set_trigger(source=0, edge=0, level=128, mode=0, pretrigger_samples=0, total_samples=2048)
    sc.set_decim("cic3+fir")
    for c in (0, 1):
        sc.set_channel(c, enable=True, coupling=0, range_index=3, dcoffset=0)
    time.sleep(0.2)
    k = 0
    try:
        for fc in CELLS:
            pp = min(plans.values(), key=lambda q: abs(q.f_actual - fc))
            sc.set_timebase(divider=pp.divider)
            sc.set_trigger(pretrigger_samples=0, total_samples=pp.total_samples)
            for var in ("host", "imm", "auto"):
                sc.set_trigger(level=255 if var == "auto" else 128)
                for i in range(REPEATS):
                    sc.set_gen(enable=False)
                    time.sleep(0.05)
                    sc.set_gen(enable=True, phase_inc=pp.phase_inc, threshold=1 << 31)
                    if var != "imm":
                        time.sleep(pp.settle_s(10) + 0.04)
                    cap = sc.capture(["CH1", "CH2"], timeout_s=2 * pp.record_s + 1.0, keep_raw=True)
                    if cap is None:
                        print(fc, var, i, "no capture")
                        continue
                    c1, c2 = cap.channels["CH1"], cap.channels["CH2"]
                    key = f"c{k:03d}"
                    k += 1
                    data[key + "_meta"] = np.array([pp.f_requested, pp.f_actual, pp.m, pp.n, pp.phase_inc, pp.record,
                                                    c1.volts_per_lsb, c2.volts_per_lsb,
                                                    ("host", "imm", "auto").index(var), int(cap.triggered)])
                    data[key + "_codes1"] = np.asarray(c1.codes, np.float64)
                    data[key + "_codes2"] = np.asarray(c2.codes, np.float64)
                    if c1.samples16 is not None:
                        data[key + "_s1"] = np.asarray(c1.samples16, np.uint16)
                        data[key + "_s2"] = np.asarray(c2.samples16, np.uint16)
                    ms = B.measure_pair(c1.volts, c2.volts, pp, vpl1=c1.volts_per_lsb, vpl2=c2.volts_per_lsb,
                                        clipped1=c1.clipped, clipped2=c2.clipped, codes1=c1.codes, codes2=c2.codes)
                    h = ms.h
                    print(f"{pp.f_actual:10.2f} Hz {var:4s} {i}: {B.db(abs(h)):7.3f} dB "
                          f"{np.degrees(np.angle(h)):8.2f} deg leak {ms.leak_db:6.1f} unsettled {ms.unsettled} "
                          f"trig {int(cap.triggered)} first {np.round(c1.codes[:6], 1)}")
    finally:
        sc.set_gen(enable=False)
        for c in (0, 1):
            s = saved_ch[c]
            sc.set_channel(c, range_index=s.range, coupling=s.coupling, dcoffset=s.dcoffset)
        sc.set_trigger(source=saved_tr.source, edge=saved_tr.edge, level=saved_tr.level, mode=saved_tr.mode,
                       pretrigger_samples=saved_tr.pretrigger_samples, total_samples=saved_tr.total_samples)
        np.savez_compressed(OUT, **data)
        print("saved", OUT, k, "captures; restore the time base and the Filter by hand if they matter")
