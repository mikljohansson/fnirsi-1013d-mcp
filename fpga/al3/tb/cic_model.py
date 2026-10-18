#!/usr/bin/env python3
"""Reference model for the 0x1534 decimation filter and the 0x1535 compensation FIR
(fpga/al3/src/zaklad.v, decim_chan and fir_chan).

The model is written independently of the RTL structure: it computes each stored sample as a
direct-form FIR over the 200 MSa/s sample stream, with the impulse response
h = (boxcar of N ones) convolved with itself K times (K = 1 is the boxcar sum of N samples),
then out = min(65535, y >> shift). The RTL uses a polyphase prefilter plus a CIC on the pair
stream; agreement on random data checks that decomposition, the 48-bit modulo arithmetic,
the shifter, the saturation and the pipeline latency.

Usage:
  cic_model.py check <edges.log>   check every logged memory write (tb_decim.v output)
  cic_model.py table               gain / resolution / alias / droop table (README section 3)

FIR segments (0x1535, configuration marker field 8 = 1): the CIC above decimates by N/2
(impulse response boxcar(N/2)^K, gain (N/2)^K, then min(65535, y >> shift) per CIC sample),
and a 15-tap FIR over consecutive CIC samples (taps from fir_design.py, coeffs K 15 0.5)
decimates by 2: out = clamp(0..65535, (sum + 2^16) >> 17).

Sample order: for each write clock edge the tb logs the four ADC buses. Core B is the earlier
sample of a pair (firmware convention, fpga_read_sample_data) unless the swap bit is set.
"""
import math
import os
import sys

FIR_T = 15                       # 0x1535 FIR taps
FIR_ALPHA = 0.5                  # passband edge / (fo/2): flat to fo/4
EXP_LAT = 12                     # pin capture to memory write (write clocks), CIC path
EXP_LAT_FIR = 25                 # same with the FIR


def fir_taps(k):
    """The 15 quantised taps of fir_design.py for K (the RTL table must equal these)."""
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import fir_design
    return [int(v) for v in fir_design.quantise(fir_design.design(k, FIR_T, FIR_ALPHA, "B"))]


def impulse(n, k):
    h = [1] * n
    for _ in range(k - 1):
        out = [0] * (len(h) + n - 1)
        for i, a in enumerate(h):
            for j in range(n):
                out[i + j] += a
        h = out
    return h


def decimation(divider):
    p = (divider >> 1) + 1
    return p, 2 * p


def rec_shift(k, n):
    return max(0, (255 * n ** k).bit_length() - 16)


def parse(path):
    segs = []
    cur = None
    with open(path) as f:
        for line in f:
            t = line.split()
            if not t:
                continue
            if t[0] == "C":
                cur = {"k": int(t[1]), "div": int(t[2]), "swap": int(t[3]), "shift": int(t[4]),
                       "nega": int(t[5]), "negb": int(t[6]), "fir": int(t[7]) if len(t) > 7 else 0,
                       "e": []}
                segs.append(cur)
            elif t[0] == "E" and cur is not None:
                cur["e"].append(tuple(int(v) for v in t[1:]))
    return [s for s in segs if s["k"] > 0 and s["e"]]


def check(path):
    segs = parse(path)
    if not segs:
        print("cic_model: no decimated segments in", path)
        return 1
    fails = 0
    total = 0
    taps = {}
    for s in segs:
        k, sw, sh, fir = s["k"], s["swap"], s["shift"], s["fir"]
        p, n = decimation(s["div"])
        if fir:
            if p % 2 or p < 8:
                print(f"K {k} N {n}: FIR segment with N not a multiple of 4 or below 16 -> FAIL")
                fails += 1
                continue
            if k not in taps:
                taps[k] = fir_taps(k)
            h = impulse(n // 2, k)
        else:
            h = impulse(n, k)
        e = s["e"]
        # contiguous edge numbers
        for a, b in zip(e, e[1:]):
            if b[0] != a[0] + 1:
                print("cic_model: edge log not contiguous at", a[0])
                return 1
        # 200 MSa/s streams per channel. Pair i = the pins at rising edge i, or for a core captured
        # on the falling edge, the pins at the falling edge after rising edge i (logged on line i + 1).
        na, nb = s["nega"], s["negb"]
        xs = []
        for ch in (0, 1):
            x = []
            for i, r in enumerate(e):
                nxt = e[i + 1] if i + 1 < len(e) else r
                pa, pb = (r[1], r[2]) if ch == 0 else (r[3], r[4])
                fa, fb = (nxt[8], nxt[9]) if ch == 0 else (nxt[10], nxt[11])
                ca = fa if na else pa
                cb = fb if nb else pb
                early, late = (ca, cb) if sw else (cb, ca)
                x += [early, late]
            xs.append(x)
        writes = [i for i, r in enumerate(e) if r[5] == 1]
        spacing = sorted(set(b - a for a, b in zip(writes, writes[1:])))
        if spacing and spacing != [p]:
            print(f"K {k} N {n}: write spacing {spacing}, expected {p}")
            fails += 1

        def cic(ch, j, m):
            idx = 2 * j + 1                       # later sample of pair j
            if idx - (len(h) - 1) < 0:
                return None
            x = xs[ch]
            y = sum(h[i] * x[idx - i] for i in range(len(h)))
            if k == 1:                              # boxcar: plain arithmetic sum of m samples
                assert y == sum(x[idx - m + 1: idx + 1])
            return min(65535, y >> sh)

        def expect(ch, j):
            if not fir:
                return cic(ch, j, n)
            # FIR over the CIC samples at pairs j, j - P/2, ..., j - 14 P/2 (newest first)
            acc = 0
            for t in range(FIR_T):
                c = cic(ch, j - t * (p // 2), n // 2)
                if c is None:
                    return None
                acc += taps[k][t] * c
            return max(0, min(65535, (acc + (1 << 16)) >> 17))

        # find the latency (write edge - last pair used) that explains every checkable write
        exp_lat = EXP_LAT_FIR if fir else EXP_LAT
        best = None
        for lat in [exp_lat] + [v for v in range(0, 64) if v != exp_lat]:
            ok = cnt = 0
            for w in writes:
                j = w - lat
                if j < 0:
                    continue
                e1, e2 = expect(0, j), expect(1, j)
                if e1 is None:
                    continue
                dia = e[w][7]
                cnt += 1
                if (dia & 0xFFFF) == e1 and (dia >> 16) == e2:
                    ok += 1
            if cnt and ok == cnt:
                best = (lat, cnt)
                break
        sat = sum(1 for w in writes if (e[w][7] & 0xFFFF) == 0xFFFF)
        zero = sum(1 for w in writes if (e[w][7] & 0xFFFF) == 0) if fir else 0
        mode = "FIR" if fir else "CIC"
        if best is None:
            print(f"{mode} K {k} N {n} swap {sw} shift {sh}: no latency matches all writes -> FAIL")
            fails += 1
        else:
            lat, cnt = best
            st = "ok" if lat == exp_lat else f"FAIL (expected {exp_lat})"
            print(f"{mode} K {k} N {n:5d} swap {sw} neg {na}{nb} shift {sh:2d}: {cnt:4d} writes match, latency {lat} write clocks {st}"
                  f"{', ' + str(sat) + ' saturated' if sat else ''}{', ' + str(zero) + ' clamped at 0' if zero else ''}")
            if lat != exp_lat:
                fails += 1
            total += cnt
    print(f"cic_model: {len(segs)} segments, {total} samples checked, {fails} failures ->",
          "PASS" if fails == 0 else "FAIL")
    return 0 if fails == 0 else 1


def mag_db(k, n, f):
    """|H(f)| relative to DC in dB, f in units of the 200 MSa/s input rate."""
    if f == 0:
        return 0.0
    num = math.sin(math.pi * f * n)
    den = n * math.sin(math.pi * f)
    v = abs(num / den) ** k
    return 20 * math.log10(v) if v > 0 else -999.0


def table():
    print("Gain, resolution and frequency response (from the reference model, input 200 MSa/s)")
    print("N = decimation, fo = 200 MSa/s / N. Droop at B = fo/4 and fo/8 (passband edge).")
    print("Alias rejection = attenuation at fo - B, the nearest frequency that folds onto B.")
    print("Noise bits = 0.5 * log2((sum h)^2 / sum h^2): gain for white noise (ADC noise ~2 LSB).")
    print("LSB = size of one 16-bit step in 8-bit ADC LSB with the recommended shift.")
    print()
    print("| K | N | fo | shift | LSB (ADC LSB) | noise bits | droop fo/8 | droop fo/4 | alias fo/8 | alias fo/4 |")
    print("|---|---|---|---|---|---|---|---|---|---|")
    for k in (1, 2, 3):
        for n in (2, 4, 10, 20, 100, 200, 1000, 10000):
            if 255 * n ** k >= 2 ** 48:
                continue
            fo = 200e6 / n
            sh = rec_shift(k, n)
            lsb = 2 ** sh / n ** k
            # sum h^2 for the boxcar^K: computed exactly for small N, by integral for large N
            if n ** k <= 2_000_000 and n <= 2000:
                h = impulse(n, k)
                s2 = sum(v * v for v in h)
                bits = 0.5 * math.log2((n ** k) ** 2 / s2)
            else:
                # continuous approximation: sum h^2 ~ N^(2K-1) * c_K, c_1 = 1, c_2 = 2/3, c_3 = 11/20
                c = {1: 1.0, 2: 2.0 / 3.0, 3: 11.0 / 20.0}[k]
                bits = 0.5 * math.log2(n / c)
            row = [k, n, f"{fo/1e6:g} M" if fo >= 1e6 else f"{fo/1e3:g} k", sh, f"{lsb:.3g}", f"{bits:.2f}"]
            for b in (1 / 8, 1 / 4):
                row.append(f"{mag_db(k, n, b / n):.2f} dB")
            for b in (1 / 8, 1 / 4):
                row.append(f"{mag_db(k, n, (1 - b) / n):.1f} dB")
            print("| " + " | ".join(str(v) for v in row) + " |")
    print()
    print("Interleave spur: (1 + z^-1)^K has a zero at 100 MHz, so the core A/B offset and gain")
    print("mismatch tone at fs/2 is removed for every K (boxcar included, N even).")
    return 0


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "check":
        sys.exit(check(sys.argv[2]))
    if len(sys.argv) >= 2 and sys.argv[1] == "table":
        sys.exit(table())
    print(__doc__)
    sys.exit(2)
