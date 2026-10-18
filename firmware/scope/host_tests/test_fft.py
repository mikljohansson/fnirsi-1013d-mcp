#!/usr/bin/env python3
"""Host test of the target's fixed-point FFT (fx_fft.c via fft_cli) against numpy float64.

  python3 -I test_fft.py --cli build/fft_cli [--tables ../fx_fft_tables.c --gen ../../tools/gen_fft_tables.py] [-v]

Checks, for N = 256..16384 and the rect / Hann / flat-top windows:
  - the committed tables equal the generator output
  - fx_log2_q16 / fx_cdb_from_q16 against math.log2
  - Q30 windows against the float definitions
  - complex bins against numpy.fft.rfft of the same windowed input: worst bin error relative to a full-scale
    sine's bin (dBFS); must be below -100 dBFS
  - bin levels in dB against numpy for bins above -60 dBFS: within 0.05 dB
  - absolute level: a full-scale sine centred on a bin reads 20 log10(A / sqrt 2) dB on every window; flat-top
    within 0.05 dB also half a bin off
  - worst-case growth (DC full scale, alternating full scale, i32 at the 2^28 input limit) does not overflow
Prints the measured bounds (firmware/README.md "FFT core" quotes them).
"""
import argparse
import math
import shlex
import subprocess
import sys

import numpy as np

FLATTOP = (0.21557895, 0.41663158, 0.277263158, 0.083578947, 0.006947368)
fails = []


def check(cond, msg):
    if not cond:
        fails.append(msg)
        print("FAIL:", msg)


def window_float(n, kind):
    t = 2 * np.pi * np.arange(n) / n
    if kind == 0:
        return np.ones(n)
    if kind == 1:
        return 0.5 - 0.5 * np.cos(t)
    w = np.zeros(n)
    for i, a in enumerate(FLATTOP):
        w += (-1) ** i * a * np.cos(i * t)
    return w


PREFIX = []


def run(cli, log2n, kind, x, sample, offset, cdb_offset=None):
    n = 1 << log2n
    data = (np.asarray(x, dtype=np.uint8) if sample == "u8" else np.asarray(x, dtype="<i4")).tobytes()
    args = PREFIX + [cli, str(log2n), str(kind), sample, str(offset)] + ([str(cdb_offset)] if cdb_offset is not None else [])
    out = subprocess.run(args, input=data, capture_output=True, check=True).stdout
    shift, norm = np.frombuffer(out[:8], dtype="<i4")
    pos = 8
    bins = np.frombuffer(out[pos:pos + 4 * (n + 2)], dtype="<i4").astype(np.float64)
    pos += 4 * (n + 2)
    cdb = np.frombuffer(out[pos:pos + 2 * (n // 2 + 1)], dtype="<i2").astype(np.float64) / 100.0
    pos += 2 * (n // 2 + 1)
    win = np.frombuffer(out[pos:pos + 4 * n], dtype="<i4").astype(np.float64) / 2.0 ** 30
    X = (bins[0::2] + 1j * bins[1::2]) / (2.0 * 2.0 ** int(shift))
    return X, cdb, win


def ref_db(Xref, wsum, n):
    p = np.abs(Xref) ** 2 * 2 / wsum ** 2
    p[0] /= 2
    p[n // 2] /= 2
    with np.errstate(divide="ignore"):
        return 10 * np.log10(p)


def signals(n, rng):
    k = np.arange(n)
    c = n // 8
    yield "noise", "u8", rng.integers(0, 256, n), 128, 127.5
    yield "sine_fs_bin", "u8", np.round(128 + 127 * np.sin(2 * np.pi * c * k / n)), 128, 127.5
    yield "sine_offbin_plus_-60dB", "u8", np.clip(np.round(
        128 + 120 * np.sin(2 * np.pi * (c + 0.37) * k / n) + 0.12 * np.sin(2 * np.pi * (3 * c + 0.11) * k / n)
        + rng.normal(0, 0.6, n)), 0, 255), 128, 127.5
    yield "dc_full", "u8", np.full(n, 255), 0, 255.0
    yield "alternating", "u8", np.tile([0, 255], n // 2), 128, 127.5
    yield "impulse", "u8", np.where(k == 5, 255, 128), 128, 127.5
    yield "small_noise", "u8", rng.integers(127, 130, n), 128, 127.5
    lim = (1 << 28) - 1
    yield "i32_limit_noise", "i32", rng.integers(-lim, lim + 1, n), 0, float(lim)
    yield "i32_16bit_tone", "i32", np.round(32767 * np.sin(2 * np.pi * (c + 0.25) * k / n)), 0, 32767.0
    yield "i32_dc_limit", "i32", np.full(n, lim), 0, float(lim)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", required=True)
    ap.add_argument("--tables")
    ap.add_argument("--gen")
    ap.add_argument("--prefix", default="", help="runner for the CLI, e.g. qemu-arm -cpu arm926")
    ap.add_argument("-v", action="store_true")
    a = ap.parse_args()
    PREFIX[:] = shlex.split(a.prefix)
    rng = np.random.default_rng(1013)

    if a.tables and a.gen:
        gen = subprocess.run([sys.executable, "-I", a.gen], capture_output=True, check=True, text=True).stdout
        with open(a.tables, newline="") as f:
            check(f.read() == gen, "fx_fft_tables.c differs from gen_fft_tables.py output")

    # log2 / dB conversion
    vals = np.unique(np.concatenate([rng.integers(1, 2 ** 62, 20000, dtype=np.uint64),
                                     np.arange(1, 5000, dtype=np.uint64),
                                     (np.uint64(1) << np.arange(0, 63, dtype=np.uint64))]))
    out = subprocess.run(PREFIX + [a.cli, "log2"], input=vals.astype("<u8").tobytes(), capture_output=True, check=True).stdout
    r = np.frombuffer(out, dtype="<i4").reshape(-1, 2)
    exact = np.array([math.log2(int(v)) for v in vals])
    e_log2 = np.max(np.abs(r[:, 0] / 65536.0 - exact))
    e_cdb = np.max(np.abs(r[:, 1] / 100.0 - 10 * exact * math.log10(2)))
    print("log2_q16 max error %.2e octaves, dB conversion max error %.4f dB" % (e_log2, e_cdb))
    check(e_log2 < 4e-5, "log2 error %g" % e_log2)
    check(e_cdb < 0.006, "cdb error %g" % e_cdb)

    worst_bin = -999.0
    worst_db = 0.0
    print("%6s %4s %-24s %10s %9s" % ("N", "win", "signal", "bin dBFS", "dB err"))
    for log2n in range(8, 15):
        n = 1 << log2n
        for kind in range(3):
            wf = window_float(n, kind)
            wsum = wf.sum()
            for name, sample, x, off, fs in signals(n, rng):
                X, cdb, win = run(a.cli, log2n, kind, x, sample, off)
                ew = np.max(np.abs(win - wf))
                check(ew < 4e-9, "window %d N %d error %g" % (kind, n, ew))
                Xref = np.fft.rfft(wf * (np.asarray(x, dtype=np.float64) - off))
                err = np.max(np.abs(X - Xref)) / (fs * wsum / 2)
                err_dbfs = 20 * math.log10(max(err, 1e-30))
                rdb = ref_db(Xref, wsum, n)
                fs_db = 20 * math.log10(fs / math.sqrt(2))
                sel = rdb > fs_db - 60
                derr = float(np.max(np.abs(cdb[sel] - rdb[sel]))) if sel.any() else 0.0
                worst_bin = max(worst_bin, err_dbfs)
                worst_db = max(worst_db, derr)
                if a.v or err_dbfs > -100 or derr > 0.05:
                    print("%6d %4d %-24s %10.1f %9.4f" % (n, kind, name, err_dbfs, derr))
                check(err_dbfs < -100, "N %d win %d %s: bin error %.1f dBFS" % (n, kind, name, err_dbfs))
                check(derr < 0.05, "N %d win %d %s: level error %.4f dB" % (n, kind, name, derr))
                if name == "sine_fs_bin":
                    peak = cdb[n // 8]
                    want = 20 * math.log10(127 / math.sqrt(2))
                    check(abs(peak - want) < 0.02, "N %d win %d: tone %.3f dB, want %.3f" % (n, kind, peak, want))
            if kind == 1:
                # the dB offset parameter shifts every non-empty bin, empty bins stay at the floor
                x = np.round(128 + 100 * np.sin(2 * np.pi * (n // 8) * np.arange(n) / n))
                _, c0, _ = run(a.cli, log2n, kind, x, "u8", 128)
                _, c1, _ = run(a.cli, log2n, kind, x, "u8", 128, -1234)
                live = c0 > -290.0
                check(np.all(np.abs((c1[live] - c0[live]) + 12.34) < 0.006), "N %d dB offset not applied" % n)
                _, cz, _ = run(a.cli, log2n, kind, np.full(n, 128), "u8", 128, 500)
                check(np.all(cz == -320.0), "N %d empty bins not at the floor" % n)
            if kind == 2:
                k = np.arange(n)
                x = np.round(128 + 127 * np.sin(2 * np.pi * (n // 8 + 0.5) * k / n))
                _, cdb, _ = run(a.cli, log2n, kind, x, "u8", 128)
                peak = cdb.max()
                want = 20 * math.log10(127 / math.sqrt(2))
                check(abs(peak - want) < 0.05, "N %d flat-top half-bin tone %.3f dB, want %.3f" % (n, peak, want))
    print("worst bin error %.1f dBFS (limit -100), worst level error above -60 dBFS %.4f dB (limit 0.05)"
          % (worst_bin, worst_db))
    print("test_fft: %s" % ("OK" if not fails else "%d failures" % len(fails)))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
