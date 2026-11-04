#!/usr/bin/env python3
"""CH2 offset sweep at 200 MSa/s: each core's carry-error share against its level (README section 19, the acceptance
measure of an encode or capture change).

The error share of a core (samples 4 or more codes above the median of the sample and its two neighbours, enc_ab.py)
is not a property of the bitstream alone: it depends on where the signal's flat low level sits against the 0x5F / 0x60
carry, and that level ("parking") moves with the CH2 offset the wizard centres (one wizard run moved it +7 PWM units
on 2026-11-04: rel-1 6.4 % against rel-2 0.1 % on core A, same image, same probes) and with the bitstream itself
(core A - core B level over the sweep: enc-250 +0.76 codes, the release -0.44). The sweep steps CH2's offset_pwm
through the carry (one PWM unit = about 0.25 codes), takes rising-phase captures at every step (the CH1-level trigger
also fires on the falling slope, enc_ab.py; captures whose core A median is at or above --phase-split are dropped) and
records per core the error share and the raw level. The peak per core over the sweep is the parking-independent number
to compare between images; the per-level table pools the steps of one or more sweeps of the same image by level.

    python -I fpga/al3/tools/enc_sweep.py --port COM10 --image rel-2 --image-sha256 <sha256> --out sweep_rel-2.npz
    python -I fpga/al3/tools/enc_sweep.py ... --range 6:30   # offset_pwm base+6 .. base+30 (default 10: -10 .. +10)
    python -I fpga/al3/tools/enc_sweep.py --analyse sweep_rel-2_1.npz sweep_rel-2_2.npz sweep_enc-250.npz
    python -I fpga/al3/tools/enc_sweep.py --selftest                    # the analysis on synthetic data

Set up as for enc_ab.py (CH2 on the RC ladder's second node, CH1 on its input, both DC; the script sets Filter Off),
with the wizard's calibration in place: the sweep starts from the CH2 offset_pwm in effect ("base") and writes it back
at the end, also after an error (if that fails the script prints the value to restore by hand). Run it with the mcp
venv's Python; --analyse and --selftest need only numpy. Saved runs: rows (offset, A level, A %, B level, B %,
captures) and the raw captures per step (A_<offset>, B_<offset>), as the 2026-11-04 session's scratch version saved
them (its files load unchanged; --analyse pools "rel-2-s1", "rel-2-s2", ... as "rel-2").
"""
from __future__ import annotations

import argparse
import json
import re
import sys
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from enc_ab import KNOWN_IMAGES, PHASE_SPLIT, THRESH, flags, rising

CAPTURES = 6                # rising-phase captures per step (2026-11-04: 6)
MIN_LEVEL_N = 1000          # per-level table: levels with fewer samples are not printed


# ----------------------------------------------------------------------------------------------------------------------
# Analysis (numpy only)

def step_stats(caps: np.ndarray) -> tuple[float, int, int]:
    """caps: rising-phase captures x words of one core at one step -> (level = median raw code, samples, flagged)."""
    _, e = flags(caps)
    return float(np.median(caps)), int(e.size), int(e.sum())


def analyse(raw: dict) -> list[tuple]:
    """raw: {offset: (A caps, B caps)} -> rows (offset, A level, A %, B level, B %, captures) in offset order."""
    rows = []
    for off in sorted(raw):
        a, b = raw[off]
        la, na, ea = step_stats(a)
        lb, nb, eb = step_stats(b)
        rows.append((off, la, 100 * ea / max(na, 1), lb, 100 * eb / max(nb, 1), len(a)))
    return rows


def per_level(raws: list[dict]) -> dict:
    """Pool steps by level (the core's median rounded to a code): {(core, level): [samples, flagged]}."""
    out: dict = {}
    for raw in raws:
        for a, b in raw.values():
            for core, caps in (("A", a), ("B", b)):
                lv, n, e = step_stats(caps)
                c = out.setdefault((core, round(lv)), [0, 0])
                c[0] += n
                c[1] += e
    return out


def peak_line(rows: list[tuple]) -> str:
    r = np.array(rows, dtype=float)
    ia, ib = int(r[:, 2].argmax()), int(r[:, 4].argmax())
    return (f"PEAK over the sweep: A {r[ia, 2]:.2f} % at offset {int(r[ia, 0])} (level {r[ia, 1]:.1f}); "
            f"B {r[ib, 4]:.2f} % at offset {int(r[ib, 0])} (level {r[ib, 3]:.1f}); mean over the sweep A "
            f"{r[:, 2].mean():.2f} % B {r[:, 4].mean():.2f} %; A - B level {np.mean(r[:, 1] - r[:, 3]):+.2f} codes")


def report(rows: list[tuple], meta: dict) -> str:
    lines = [(f"image {meta.get('image')} ({meta.get('image_label', '-')}); base offset_pwm {meta.get('base')}; "
             f"stages {meta.get('cap_text', '-')}; {meta.get('time')}")]
    for off, la, pa, lb, pb, n in rows:
        lines.append(f"  offset {int(off):4d}: A level {la:6.1f} err {pa:6.2f} % | B level {lb:6.1f} err {pb:6.2f} %  "
                     f"({int(n)} captures)")
    lines.append(peak_line(rows))
    return "\n".join(lines)


def group_name(meta: dict) -> str:
    """The image a sweep belongs to: its label without the session's '-s<N>' repeat suffix (rel-2-s1 -> rel-2)."""
    return re.sub(r"-s\d+$", "", str(meta.get("image")))


def level_table(groups: dict[str, list[dict]]) -> str:
    """Per-level error shares of every image (its sweeps pooled), A and B side by side."""
    pooled = {g: per_level(rs) for g, rs in groups.items()}
    levels = sorted({lv for p in pooled.values() for (_, lv), (n, _) in p.items() if n >= MIN_LEVEL_N})
    names = list(groups)
    lines = [f"per level (sweeps of an image pooled; error % A / B; '-': fewer than {MIN_LEVEL_N} samples):",
             "  level " + "".join(f"{g:>20s}" for g in names)]
    for lv in levels:
        cells = []
        for g in names:
            p = pooled[g]
            part = []
            for core in ("A", "B"):
                n, e = p.get((core, lv), (0, 0))
                part.append(f"{100 * e / n:5.2f}" if n >= MIN_LEVEL_N else f"{'-':>5s}")
            cells.append(f"{part[0]:>9s} /{part[1]:>6s}   ")
        lines.append(f"  {lv:5d} " + "".join(f"{c:>20s}" for c in cells))
    return "\n".join(lines)


def load(path: str) -> tuple[dict, dict]:
    z = np.load(path, allow_pickle=False)
    meta = json.loads(str(z["meta"]))
    raw = {}
    for k in z.files:
        if k.startswith("A_"):
            off = int(k[2:])
            raw[off] = (z[k].astype(np.int16), z[f"B_{off}"].astype(np.int16))
    return raw, meta


def save(path: str, raw: dict, meta: dict) -> None:
    rows = np.array(analyse(raw), dtype=float)
    arrays = {}
    for off, (a, b) in raw.items():
        arrays[f"A_{off}"], arrays[f"B_{off}"] = a, b
    np.savez_compressed(path, rows=rows, meta=np.array(json.dumps(meta)), **arrays)


def parse_range(text: str) -> tuple[int, int]:
    """'N' -> (-N, N); 'lo:hi' -> (lo, hi), both relative to the base offset_pwm."""
    if ":" in text:
        lo, hi = (int(v) for v in text.split(":", 1))
    else:
        lo, hi = -int(text), int(text)
    if lo > hi:
        raise ValueError(f"--range {text}: low end above the high end")
    return lo, hi


# ----------------------------------------------------------------------------------------------------------------------
# Bench

def run(args) -> int:
    from fnirsi1013d import protocol as P
    from fnirsi1013d.scope import Scope

    sha = args.image_sha256.lower()
    if len(sha) != 64 or any(c not in "0123456789abcdef" for c in sha):
        print("--image-sha256: the 64-digit sha256 of the image the flash step wrote and verified")
        return 2
    lo, hi = parse_range(args.range)
    s = Scope.connect(args.port, max_response=16384)
    raw: dict = {}
    base = None
    try:
        s.ensure_control(watchdog_s=120)
        info, h = s.info, s.health()
        c1, c2 = s.get_channel("CH1", cached=False), s.get_channel("CH2", cached=False)
        if c1.coupling != 0 or c2.coupling != 0:
            print("CH1 and CH2 must be DC coupled (an AC coupled CH1 moves the trigger and so CH2's window)")
            return 2
        base = c2.offset_pwm
        tb0, dec0 = s.get_timebase(cached=False), s.get_decim()
        meta = {
            "time": time.strftime("%Y-%m-%d %H:%M:%S"), "image": args.image, "image_sha256": sha,
            "image_label": KNOWN_IMAGES.get(sha, "experiment image (README section 19)"),
            "fw": info.fw_version.rstrip(b"\0").decode(errors="replace"), "fpga_version": info.fpga_version,
            "cap_value": h.cap_value if h is not None else None,
            "cap_text": P.cap_stages_text(h.cap_value) if h is not None and h.cap_value else None,
            "enc": ({"bad": h.enc_bad, "checked": h.enc_checked} if h is not None else None),
            "ch2": {"range": c2.range, "coupling": c2.coupling, "dcoffset": c2.dcoffset, "offset_pwm": base},
            "base": base, "range": [lo, hi], "step": args.step, "captures": args.captures,
            "phase_split": args.phase_split, "gen": {"freq_hz": args.gen_hz, "duty": 0.5} if args.gen_hz else None,
            "note": args.note,
        }
        print(f"{args.image}: fw {meta['fw']} FPGA 0x{info.fpga_version:04X}, base offset_pwm {base}, stages "
              f"{meta['cap_text']}, encode self-check {meta['enc']}", flush=True)
        try:
            s.set_decim(0)
            s.set_timebase(divider=0)
            if args.gen_hz:
                s.set_gen(enable=True, freq_hz=args.gen_hz, duty=0.5)
            for off in range(base + lo, base + hi + 1, args.step):
                s.set_channel("CH2", offset_pwm=off)
                time.sleep(0.05)
                A, B, tries = [], [], 0
                while len(A) < args.captures and tries < 4 * args.captures:
                    tries += 1
                    cap = s.capture(("CH2",), timeout_s=args.timeout, keep_raw=True)
                    if cap is None:
                        continue
                    if cap.divider != 0 or cap.decim is not None:
                        raise RuntimeError(f"capture at divider {cap.divider}, decim {cap.decim}: want 0, no filter")
                    c = cap.channels["CH2"]
                    a = np.asarray(c.raw_a, dtype=np.int16)
                    if np.median(a) >= args.phase_split:
                        continue                          # falling phase
                    A.append(a)
                    B.append(np.asarray(c.raw_b, dtype=np.int16))
                if not A:
                    print(f"  offset {off}: no rising-phase capture in {tries} tries", flush=True)
                    continue
                m = min(len(x) for x in A + B)
                raw[off] = (np.stack([x[:m] for x in A]), np.stack([x[:m] for x in B]))
                _, la, pa, lb, pb, n = analyse({off: raw[off]})[0]
                print(f"  offset {off:4d}: A level {la:6.1f} err {pa:6.2f} % | B level {lb:6.1f} err {pb:6.2f} %  "
                      f"({n} captures)", flush=True)
        finally:
            try:
                s.set_channel("CH2", offset_pwm=base)
                s.set_timebase(divider=tb0.divider)
                s.set_decim(dec0.filter, dec0.flags)
                s.end_control()
            except Exception:
                print(f"RESTORE FAILED: set CH2 offset_pwm back to {base} by hand (SET_CHANNEL)")
                raise
    finally:
        s.close()
    if not raw:
        print("no step gave a rising-phase capture")
        return 1
    print(peak_line(analyse(raw)))
    if args.out:
        save(args.out, raw, meta)
        print(f"saved {args.out}")
    return 0


# ----------------------------------------------------------------------------------------------------------------------
# Self-test

def synth_step(rng, level: float, peak_at: float, peak: float, words: int = 1500, n: int = 6) -> np.ndarray:
    """n captures of a flat level with 0.6-code noise; isolated samples made 5 codes high with a probability that peaks
    (Gaussian, 0.7 codes wide) at peak_at: a core whose carry errors depend on where its level parks."""
    p = peak * np.exp(-0.5 * ((level - peak_at) / 0.7) ** 2)
    out = []
    for _ in range(n):
        x = np.round(level + rng.normal(0, 0.6, words)).astype(np.int16)
        hit = rng.random(words) < p
        hit[[0, -1]] = False
        hit[1:] &= ~hit[:-1]
        x[hit] += 5
        out.append(x)
    return np.stack(out)


def selftest() -> int:
    rng = np.random.default_rng(7)
    raw_r, raw_e = {}, {}
    for k, off in enumerate(range(853, 874)):        # 21 steps of 0.25 codes, levels 93 .. 98
        lv = 93.0 + 0.25 * k
        raw_r[off] = (synth_step(rng, lv, 97.0, 0.07), synth_step(rng, lv - 0.4, 95.0, 0.006))
        raw_e[off] = (synth_step(rng, lv + 1.2, 96.0, 0.02), synth_step(rng, lv, 97.0, 0.11))
    rows_r, rows_e = analyse(raw_r), analyse(raw_e)
    print(report(rows_r, {"image": "rel-s1", "base": 863}))
    ra, re_ = np.array(rows_r), np.array(rows_e)
    ok = 96.0 <= ra[ra[:, 2].argmax(), 1] <= 98.0 and 4.0 < ra[:, 2].max() < 9.0 and ra[:, 4].max() < 1.5
    ok &= re_[:, 4].max() > 7.0 and re_[:, 2].max() < 3.5
    ok &= abs(np.mean(re_[:, 1] - re_[:, 3]) - 1.2) < 0.3
    # phase filter: a falling-phase capture is recognised by core A's median
    hi = synth_step(rng, 181.0, 97.0, 0.0, n=2)
    ok &= bool(rising(raw_r[853][0]).all()) and not rising(hi).any() and PHASE_SPLIT == 140
    txt = level_table({"rel": [raw_r, raw_r], "enc-250": [raw_e]})
    print(txt)
    pl = per_level([raw_r])
    n97, e97 = pl[("A", 97)]
    ok &= n97 > 0 and 4.0 < 100 * e97 / n97 < 9.0 and "enc-250" in txt
    ok &= parse_range("10") == (-10, 10) and parse_range("6:30") == (6, 30)
    ok &= group_name({"image": "rel-2-s3"}) == "rel-2" and THRESH == 4
    print("selftest:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", default="COM10")
    ap.add_argument("--image", help="label of the flashed image (repeats of one image: the same label)")
    ap.add_argument("--image-sha256", help="sha256 of the flashed image (the flash step's verified read-back)")
    ap.add_argument("--range", default="10", help="offset_pwm steps around the base: N = -N..+N, or lo:hi (default 10)")
    ap.add_argument("--step", type=int, default=1, help="offset_pwm step (1 = about 0.25 codes)")
    ap.add_argument("--captures", type=int, default=CAPTURES, help="rising-phase captures per step")
    ap.add_argument("--phase-split", type=float, default=PHASE_SPLIT,
                    help="core A median at or above this = falling phase, dropped (default 140)")
    ap.add_argument("--gen-hz", type=float, default=1000.0, help="the calibration post's square (0: leave it)")
    ap.add_argument("--timeout", type=float, default=1.0)
    ap.add_argument("--note", default="")
    ap.add_argument("--out", help="save rows, raw captures and settings (.npz)")
    ap.add_argument("--analyse", nargs="+", metavar="NPZ", help="reprint saved sweeps and the per-level table")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if a.analyse:
        groups: dict[str, list[dict]] = {}
        for p in a.analyse:
            raw, meta = load(p)
            print(f"== {p}")
            print(report(analyse(raw), meta))
            groups.setdefault(group_name(meta), []).append(raw)
        print(level_table(groups))
        return 0
    if not a.image or not a.image_sha256:
        ap.error("a bench run needs --image and --image-sha256")
    return run(a)


if __name__ == "__main__":
    sys.exit(main())
