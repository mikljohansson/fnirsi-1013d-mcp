#!/usr/bin/env python3
"""Run FFT_BENCH (0x0F02, fw 0.2.27+) on the scope and print the timing table (bench PC, needs pyserial).

    python fft_bench.py COM10 [--reps 3]

The scope times its fixed-point FFT on the CH1 trace buffer (docs/protocol.md section 14 row 44). The UI pass waits
meanwhile (about 0.15 s at 3 reps). Exit status 0 on a valid response.
"""

import argparse
import importlib.util
import os
import struct
import sys

spec = importlib.util.spec_from_file_location("bench_link", os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                                       "bench_link.py"))
bench_link = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench_link)

FFT_BENCH = 0x0F02


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--reps", type=int, default=3)
    a = ap.parse_args()
    L = bench_link.Link(a.port)
    status, body, dt = L.request(FFT_BENCH, bytes([a.reps]), timeout=5.0)
    if status != 0 or body is None:
        print("FFT_BENCH failed: status %s" % status)
        return 1
    n, reps, cp15 = struct.unpack_from("<HHI", body)
    print("FFT_BENCH: %d entries, reps %d, round trip %.0f ms, CP15 control 0x%08x (MMU %d, D-cache %d, I-cache %d)"
          % (n, reps, dt * 1000, cp15, cp15 & 1, (cp15 >> 2) & 1, (cp15 >> 12) & 1))
    print("%6s %5s %9s %10s %10s %9s %6s %8s" % ("N", "mem", "init ms", "run min", "run max", "dB ms", "peak", "dB"))
    for i in range(n):
        N, win, mem, init, rmin, rmax, cdb, peak, pv = struct.unpack_from("<HBBIIIIHh", body, 8 + 24 * i)
        print("%6d %5s %9.3f %10.3f %10.3f %9.3f %6d %8.2f"
              % (N, "SRAM" if mem else "DDR", init / 1e3, rmin / 1e3, rmax / 1e3, cdb / 1e3, peak, pv / 100))
    return 0


if __name__ == "__main__":
    sys.exit(main())
