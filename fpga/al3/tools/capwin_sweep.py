#!/usr/bin/env python3
"""Per-core, per-code-band noise of every ADC capture stage (FPGA 0x153A and later; fpga/al3/README.md section 18).

The 0x36 outlier counters only see words more than 32 codes off the previous word. A stage at the edge of a core's
valid window can garble only some bits, by a few codes, near some codes (bench 2026-10-22, 0x153D: CH2 A at stages 2, 4
and 5 2..13 codes rms on a slow slope, 0x36 = 0). This script measures what the counters miss: for each core and each
stage it takes raw 200 MSa/s captures of the signal at the inputs and reports, per 8-code band of the core's running
median, the rms of (sample - running median of 9 samples of the same core), the share of samples 4 / 8 codes off, which
bits are wrong in those samples, and how many bits the chip's other core switched next to them.

Needs: a PC_DEBUG firmware image (DEBUG_RAW capability: the script writes 0x63 and reads 0x36 through DEBUG_FPGA), the
scope at 200 MSa/s (divider 0) with Filter Off, a signal that crosses the codes of interest slowly (the bench: the RC
ladder's second node with the 1 kHz square on CH2, CH1 on its input; any slow ramp or triangle works), and a trigger
that fires. Back up SD sectors 700-710 first: a stage that garbles enough to be flagged three times in a minute makes
the firmware's guard (fw 0.2.105, cap_stage.h) move that core and store the result in sector 708. The script stops when
it sees the guard act (the 0x63 value changed under it or GET_HEALTH reports the move) and always writes the original
0x63 back at the end.

    python -I fpga/al3/tools/capwin_sweep.py --port COM10 --out capwin.npz
    python -I fpga/al3/tools/capwin_sweep.py --port COM10 --cores "CH2 A" --stages 2,3,4,5 --captures 8
    python -I fpga/al3/tools/capwin_sweep.py --analyse capwin.npz      # print the report of a saved run again
    python -I fpga/al3/tools/capwin_sweep.py --selftest                # the analysis on synthetic data, no scope

Run it with the mcp venv's Python (mcp/.venv), which has the fnirsi1013d package; --analyse and --selftest need only
numpy. Output: the report on stdout, and with --out an .npz with every capture's raw cores (keys "c{core}_s{stage}_a",
"..._b": uint8, captures x words; "meta": JSON) for later analysis.
"""
from __future__ import annotations

import argparse
import json
import sys
import time

import numpy as np

CORES = ("CH1 A", "CH1 B", "CH2 A", "CH2 B")       # the 0x36 / 0x63 order
BAND = 8                                          # codes per band
MIN_BAND_N = 200                                  # samples a band needs to be reported
BAD1, BAD2 = 4, 8                                 # |residual| thresholds, codes
DBG_WRITE_CMD, DBG_WRITE_DATA, DBG_READ_DATA = 1, 2, 3


# ----------------------------------------------------------------------------------------------------------------------
# Analysis (numpy only)

def running_median(x: np.ndarray, k: int = 9) -> np.ndarray:
    """Running median of k samples, centred; the k // 2 samples at each end are NaN."""
    from numpy.lib.stride_tricks import sliding_window_view
    out = np.full(x.shape, np.nan)
    h = k // 2
    out[h:len(x) - h] = np.median(sliding_window_view(x.astype(np.float64), k), axis=1)
    return out


def popcount8(v: np.ndarray) -> np.ndarray:
    return np.unpackbits(v.astype(np.uint8)[:, None], axis=1).sum(axis=1)


def core_stats(x: np.ndarray, partner_toggles: np.ndarray) -> dict:
    """x: one core's samples of one capture (uint8); partner_toggles: per sample, the number of bits the chip's other
    core switched between its two samples on either side of this one (same length, -1 where unknown). Returns the
    per-sample arrays the summaries need (all of the same length, NaN-free)."""
    med = running_median(x)
    ok = ~np.isnan(med)
    x = x[ok].astype(np.int32)
    med = med[ok]
    res = x - med
    # the one bit whose flip brings the sample back within 2.5 codes of the median (the bit that was caught wrong);
    # 8 = no single bit explains it (several bits, or not a bit error)
    err = np.stack([np.abs((x ^ (1 << i)) - med) for i in range(8)])
    best = np.argmin(err, axis=0)
    wrong = np.where(err[best, np.arange(len(x))] <= 2.5, best, 8).astype(np.int8)
    return {"res": res, "med": med, "wrong": wrong, "ptog": partner_toggles[ok]}


def partner_toggles(a: np.ndarray, b: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """Word j holds B sample j and A sample j, A encoded T/2 after B (B first; README section 10). While A sample j is
    on the pins, B's outputs go from sample j to j + 1 (with the encodes in antiphase and data-align off, B's transition
    lies in the middle of A's valid window); while B sample j is on the pins, A goes from j - 1 to j. Returns, per
    sample, 2 x 8 bits: the partner's switching inside this sample's window (high byte) and, as the control, its
    switching one sample earlier or later, outside the window (low byte); -1 where unknown."""
    a8, b8 = a.astype(np.uint8), b.astype(np.uint8)
    pa = popcount8(a8[:-1] ^ a8[1:])                 # A j -> j + 1
    pb = popcount8(b8[:-1] ^ b8[1:])                 # B j -> j + 1
    ta = np.full(len(a), -1, dtype=np.int32)
    tb = np.full(len(b), -1, dtype=np.int32)
    ta[1:-1] = (pb[1:] << 8) | pb[:-1]               # in: B j -> j + 1; control: B j - 1 -> j
    tb[1:-1] = (pa[:-1] << 8) | pa[1:]               # in: A j - 1 -> j; control: A j -> j + 1
    return ta, tb


def summarise(per: list[dict]) -> dict:
    """Merge the per-capture arrays of one core and stage and reduce them to the report numbers."""
    res = np.concatenate([p["res"] for p in per])
    med = np.concatenate([p["med"] for p in per])
    wrong = np.concatenate([p["wrong"] for p in per])
    ptog = np.concatenate([p["ptog"] for p in per])
    out: dict = {"n": len(res), "rms": float(np.sqrt(np.mean(res ** 2))) if len(res) else float("nan"),
                 "bands": {}}
    band = (np.clip(np.rint(med), 0, 255) // BAND).astype(int)
    for k in np.unique(band):
        sel = band == k
        r = res[sel]
        if len(r) < MIN_BAND_N:
            continue
        out["bands"][int(k * BAND)] = {
            "n": len(r), "rms": float(np.sqrt(np.mean(r ** 2))), "max": float(np.max(np.abs(r))),
            "bad4": float(np.mean(np.abs(r) >= BAD1)), "bad8": float(np.mean(np.abs(r) >= BAD2))}
    bad = np.abs(res) >= BAD1
    out["bad4"] = float(np.mean(bad)) if len(res) else 0.0
    out["n_bad4"] = int(bad.sum())
    # share of the bad samples explained by a single wrong bit i (0 .. 7), and by none (index 8)
    out["bit_share"] = [float(np.mean(wrong[bad] == i)) if bad.any() else 0.0 for i in range(9)]
    sel = bad & (ptog >= 0)
    out["ptog_in"] = float((ptog[sel] >> 8).mean()) if sel.any() else float("nan")
    out["ptog_ctl"] = float((ptog[sel] & 0xFF).mean()) if sel.any() else float("nan")
    return out


def analyse(raw: dict[tuple[int, int], tuple[np.ndarray, np.ndarray]]) -> dict[tuple[int, int, int], dict]:
    """raw[(core, stage)] = (A captures, B captures) of the swept core's channel, each captures x words (uint8).
    Returns {(swept core, stage, reported core): summary}; both cores of the channel are reported (the partner is the
    reference: same input, its own stage unchanged)."""
    out = {}
    for (core, stage), (A, B) in sorted(raw.items()):
        ch = core // 2
        per = {2 * ch: [], 2 * ch + 1: []}
        for a, b in zip(A, B):
            ta, tb = partner_toggles(a, b)
            per[2 * ch].append(core_stats(a, ta))
            per[2 * ch + 1].append(core_stats(b, tb))
        for c, p in per.items():
            out[(core, stage, c)] = summarise(p)
    return out


def report(res: dict[tuple[int, int, int], dict], counts: dict[tuple[int, int], list] | None = None) -> str:
    lines = []
    swept = sorted({(k[0]) for k in res})
    for core in swept:
        stages = sorted({k[1] for k in res if k[0] == core})
        lines.append(f"== {CORES[core]} swept (stages {', '.join(map(str, stages))}); its partner "
                     f"{CORES[core ^ 1]} keeps its stage")
        for c in (core, core ^ 1):
            bands = sorted({b for s in stages for b in res[(core, s, c)]["bands"]})
            lines.append(f"  {CORES[c]}: rms of (sample - running median 9) per band of the median, codes"
                         + ("" if c == core else " (reference)"))
            lines.append("    stage  all   " + " ".join(f"{b:>3}-{b + BAND - 1:<3}" for b in bands))
            for s in stages:
                r = res[(core, s, c)]
                cells = []
                for b in bands:
                    v = r["bands"].get(b)
                    cells.append(f"{v['rms']:7.2f}" if v else "      -")
                lines.append(f"    {s:>5} {r['rms']:5.2f}  " + " ".join(cells))
        lines.append(f"  {CORES[core]}: samples >= {BAD1} codes off; the share of them that one wrong bit explains (bit "
                     "0 .. 7, then none); the bits the partner core switched while they were on the pins (mean; in brackets the "
                     "partner's switching one sample away, the control)")
        for s in stages:
            r = res[(core, s, core)]
            cnt = ""
            if counts and (core, s) in counts:
                cc = np.array(counts[(core, s)])
                cnt = f"  0x36 sum {int(cc[:, core].sum())} (partner {int(cc[:, core ^ 1].sum())})"
            lines.append(f"    stage {s}: {r['n_bad4']:6d} of {r['n']} ({100 * r['bad4']:.3f} %)  bits "
                         + " ".join(f"{v:.2f}" for v in r["bit_share"])
                         + f"  partner toggles {r['ptog_in']:.2f} (control {r['ptog_ctl']:.2f}){cnt}")
    lines.append("Reading: a stage whose rms in a band exceeds the partner's and the core's best stage there is at the "
                 "edge of its window; bits concentrated in one or two columns point at those pins (per-pin skew or "
                 "crosstalk), 'none' (several bits at once) at the core's own transition, partner toggles well above "
                 "the control at the other core's output switching (ground bounce, crosstalk).")
    return "\n".join(lines)


# ----------------------------------------------------------------------------------------------------------------------
# Bench

def fpga_write(s, cmd: int, data: bytes) -> None:
    s.debug_fpga([(DBG_WRITE_CMD, cmd), (DBG_WRITE_DATA, data)])


def read_0x36(s) -> tuple[int, list[int]]:
    d = s.debug_fpga([(DBG_WRITE_CMD, 0x36), (DBG_READ_DATA, 10)])
    if len(d) != 10:
        raise RuntimeError(f"0x36 read returned {len(d)} bytes")
    return (d[0] << 8) | d[1], [(d[2 + 2 * i] << 8) | d[3 + 2 * i] for i in range(4)]


def with_stage(v: int, core: int, stage: int) -> int:
    return (v & ~(7 << (3 * core))) | (stage << (3 * core))


def guard_moved(s) -> bool:
    h = s.health()
    d = h.describe_capture() if h is not None else None
    return bool(d and d.get("moved_by_guard"))


def sweep(args) -> int:
    from fnirsi1013d import protocol as P
    from fnirsi1013d.scope import Scope

    cores = [CORES.index(c.strip()) for c in args.cores.split(",")]
    stages = [int(x) for x in args.stages.split(",")]
    if any(not 0 <= st <= 5 for st in stages):
        print("stages are 0..5")
        return 2
    s = Scope.connect(args.port)
    try:
        if not s.profile().has(P.Caps.DEBUG_RAW):
            print("this firmware has no DEBUG_RAW (DEBUG_FPGA): load a PC_DEBUG image")
            return 2
        orig, _ = read_0x36(s)
        moved0 = guard_moved(s)
        print(f"0x63 now 0x{orig:04X}; guard moved this boot: {moved0}")
        raw: dict[tuple[int, int], tuple[list, list]] = {}
        counts: dict[tuple[int, int], list] = {}
        meta = {"orig": orig, "cores": cores, "stages": stages, "captures": args.captures, "time": time.time()}
        try:
            for core in cores:
                ch = core // 2
                for st in stages:
                    v = with_stage(orig, core, st)
                    fpga_write(s, 0x63, bytes((v >> 8, v & 0xFF)))
                    A, B, cnt = [], [], []
                    tries = 0
                    while len(A) < args.captures:
                        tries += 1
                        if tries > 3 * args.captures:
                            raise RuntimeError(f"{CORES[core]} stage {st}: no trigger in {tries - 1} tries")
                        cap = s.capture(("CH1", "CH2"), timeout_s=args.timeout, keep_raw=True)
                        if cap is None:
                            continue
                        if cap.divider != 0 or cap.decim is not None:
                            raise RuntimeError(f"the capture is not 200 MSa/s with Filter Off (divider {cap.divider}, "
                                               f"decim {cap.decim}); set the time base and the filter first")
                        now, c4 = read_0x36(s)
                        if now != v:
                            raise RuntimeError(f"0x63 changed under the sweep: 0x{now:04X}, expected 0x{v:04X} "
                                               "(the firmware's guard moved a core?)")
                        chd = cap.channels[("CH1", "CH2")[ch]]
                        A.append(np.asarray(chd.raw_a, dtype=np.uint8))
                        B.append(np.asarray(chd.raw_b, dtype=np.uint8))
                        cnt.append(c4)
                    n = min(len(x) for x in A + B)
                    raw[(core, st)] = (np.stack([x[:n] for x in A]), np.stack([x[:n] for x in B]))
                    counts[(core, st)] = cnt
                    print(f"  {CORES[core]} stage {st}: {len(A)} captures of {n} words, 0x36 "
                          f"{[int(sum(c[i] for c in cnt)) for i in range(4)]}", flush=True)
                    if not moved0 and guard_moved(s):
                        raise RuntimeError("GET_HEALTH reports that the guard moved a core: stopped (restore sector "
                                           "708 from the backup or Calibrate again)")
        finally:
            fpga_write(s, 0x63, bytes((orig >> 8, orig & 0xFF)))
            back, _ = read_0x36(s)
            print(f"0x63 restored: 0x{back:04X}")
    finally:
        s.close()
    res = analyse(raw)
    print(report(res, counts))
    if args.out:
        arrays = {}
        for (core, st), (A, B) in raw.items():
            arrays[f"c{core}_s{st}_a"] = A
            arrays[f"c{core}_s{st}_b"] = B
            arrays[f"c{core}_s{st}_cnt"] = np.array(counts[(core, st)], dtype=np.uint16)
        np.savez_compressed(args.out, meta=np.array(json.dumps(meta)), **arrays)
        print(f"saved {args.out}")
    return 0


def load(path: str):
    z = np.load(path, allow_pickle=False)
    raw, counts = {}, {}
    for k in z.files:
        if k.endswith("_a"):
            core, st = (int(t[1:]) for t in k.split("_")[:2])
            raw[(core, st)] = (z[k], z[k[:-2] + "_b"])
            if k[:-2] + "_cnt" in z.files:
                counts[(core, st)] = z[k[:-2] + "_cnt"].tolist()
    return raw, counts


def selftest() -> int:
    """Synthetic slow ramp through codes 90..130 on both cores, 1 code rms noise; the A core at 'stage 4' gets bit 3
    wrong whenever bit 3 differs between neighbouring samples (a late pin), at 'stage 3' nothing. The report must show
    stage 4 above stage 3 in the affected bands with the errors in bit 3."""
    rng = np.random.default_rng(1)
    n = 16000
    t = np.linspace(130, 90, n)
    raw = {}
    for st in (3, 4):
        A, B = [], []
        for _ in range(2):
            a = np.clip(np.rint(t + rng.normal(0, 1, n)), 0, 255).astype(np.uint8)
            b = np.clip(np.rint(t + rng.normal(0, 1, n)), 0, 255).astype(np.uint8)
            if st == 4:
                late = ((a[1:] ^ a[:-1]) & 8) != 0
                a[1:][late] ^= 8                    # bit 3 keeps the previous sample's value
            A.append(a)
            B.append(b)
        raw[(2, st)] = (np.stack(A), np.stack(B))
    res = analyse(raw)
    print(report(res))
    r3, r4 = res[(2, 3, 2)], res[(2, 4, 2)]
    ok = r4["rms"] > 1.3 * r3["rms"] and int(np.argmax(r4["bit_share"])) == 3
    print("selftest:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--port", default="auto", help="serial port or tcp://host:port (default: auto-detect)")
    ap.add_argument("--cores", default=",".join(CORES), help='cores to sweep, e.g. "CH2 A,CH2 B" (default: all four)')
    ap.add_argument("--stages", default="2,3,4,5", help="stages to try (default 2,3,4,5; 0 and 1 are inside the "
                    "transition at 200 MSa/s and make the guard act)")
    ap.add_argument("--captures", type=int, default=8, help="captures per core and stage (default 8)")
    ap.add_argument("--timeout", type=float, default=2.0, help="trigger wait per capture, s")
    ap.add_argument("--out", help="save the raw cores and 0x36 counts to this .npz")
    ap.add_argument("--analyse", metavar="NPZ", help="report a saved run (no scope)")
    ap.add_argument("--selftest", action="store_true", help="run the analysis on synthetic data (no scope)")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if args.analyse:
        raw, counts = load(args.analyse)
        print(report(analyse(raw), counts))
        return 0
    return sweep(args)


if __name__ == "__main__":
    sys.exit(main())
