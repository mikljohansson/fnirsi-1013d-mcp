#!/usr/bin/env python3
"""A/B of CH2's code errors between FPGA images (README section 19; the encode-phase experiment, roadmap section 6 item 4).

The bench's acceptance measure for 0x153C / 0x153D (docs/hardware-status.md 2026-10-22 11:36 and 18:27-18:33): raw CH2
captures with Filter Off, per core (A, B), the share of samples 4 or more codes ABOVE the median of the sample and its two
neighbours of the same core (the errors are one-sided: README section 18), overall, in the "hot zone" of medians
0x5E..0x6F, and per 8-code band of the median. This script is `e_ab.py` (16 captures per core at dividers 0 and 99) and
`e_ab2.py` (32 captures per core at divider 0, per band, raw saved) of 2026-10-22 in one, recovered from the lead's
scratchpad of that day; differences: every capture is used for both cores (the originals took separate captures per
core), a capture without a trigger is retried instead of crashing, the settings are restored as they were, and the run
records what makes runs comparable (below).

    python -I fpga/al3/tools/enc_ab.py --port COM10 --image enc0 --image-sha256 <sha256 of the flashed image> --out ab_enc0.npz
    python -I fpga/al3/tools/enc_ab.py --analyse ab_rel.npz ab_enc0.npz ...   # reprint and compare saved runs
    python -I fpga/al3/tools/enc_ab.py --selftest                             # the analysis on synthetic data

Defaults: dividers 0 (200 MSa/s, 32 captures) and 99 (2 MSa/s, 16 captures), the generator on at 1 kHz 50 % (the
calibration post: CH2 on the RC ladder's second node, as the 2026-10-22 runs), trigger mode as the scope has it. Set up
the probes, the CH2 range and the offset BEFORE the first run and leave them alone for every image: the error share
depends on where the signal's flat levels sit against the 0x5F / 0x60 carry (hardware-status 18:27-18:33), so the
script records CH2's range, coupling and offset (dcoffset / offset_pwm), the trained capture stages in effect (GET_HEALTH
cap_value), the encode self-check counters, firmware and FPGA version words and the image's sha256 / label you pass
(the firmware cannot see which image is flashed: every experiment image reports 0x153D). --analyse warns when two runs
differ in any of them.

Run it with the mcp venv's Python (mcp/.venv: the fnirsi1013d package); --analyse and --selftest need only numpy.
"""
from __future__ import annotations

import argparse
import json
import sys
import time

import numpy as np

HOT = (0x5E, 0x6F)          # the hot zone of the 2026-10-22 A/B (medians at the 0x5F / 0x60 carry and above)
BAND = 8
MIN_BAND_N = 200
THRESH = 4                  # codes above the 3-sample median

#: images the bench has flashed or will flash (sha256 of the raw .bin or of the 1 MiB padded flash image -> label);
#: README section 19 lists the experiment images with their padded hashes
KNOWN_IMAGES = {
    "fd0be7a2c715e3106b1501986640cdf64a1c8af64c534502701e5d836d32e96a": "0x153D release (gated encodes), raw",
    "aa252fb7e94dca01f064a69d79d813d974024ab77763917a6f5dd9051a1e63cc": "0x153D release (gated encodes), flash image",
    "2580b090db3dd16f5609ddcb0d27576f9dfe6a4651dbafa85000d807a6bec7f2": "0x153C, raw",
    "07393bf5a728542b7de0738ca8aab7ccbf3b00e7267ca1efd8e66f8c2d361f77": "0x153C, flash image",
}
#: keys of the recorded settings that must match for two runs to be comparable
COMPARE_KEYS = ("ch2", "cap_value", "fw", "gen", "trigger_mode")


# ----------------------------------------------------------------------------------------------------------------------
# Analysis (numpy only)

def flags(x: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    """(median of x[j-1], x[j], x[j+1], flagged = x[j] - median >= THRESH) for j = 1 .. n - 2."""
    x = x.astype(np.int16)
    med = np.median(np.stack([x[:-2], x[1:-1], x[2:]]), axis=0)
    return med, (x[1:-1] - med) >= THRESH


def core_stats(caps: np.ndarray) -> dict:
    """caps: captures x words (uint8) of one core."""
    n = bad = hot_n = hot_bad = 0
    band_n = np.zeros(256 // BAND, dtype=np.int64)
    band_b = np.zeros(256 // BAND, dtype=np.int64)
    for x in caps:
        med, e = flags(x)
        n += e.size
        bad += int(e.sum())
        m = (med >= HOT[0]) & (med <= HOT[1])
        hot_n += int(m.sum())
        hot_bad += int((e & m).sum())
        b = (med // BAND).astype(int)
        np.add.at(band_n, b, 1)
        np.add.at(band_b, b, e.astype(np.int64))
    return {"n": n, "bad": bad, "pct": 100 * bad / max(n, 1), "hot_n": hot_n, "hot_bad": hot_bad,
            "hot_pct": 100 * hot_bad / max(hot_n, 1), "band_n": band_n, "band_b": band_b}


def analyse(raw: dict) -> dict:
    """raw: {(divider, core): captures x words} -> {(divider, core): stats}."""
    return {k: core_stats(v) for k, v in raw.items()}


def report(res: dict, meta: dict) -> str:
    lines = [f"image {meta.get('image')} ({meta.get('image_label')}) sha256 {str(meta.get('image_sha256'))[:16]}...; "
             f"fw {meta.get('fw')} FPGA 0x{meta.get('fpga_version', 0):04X}; stages {meta.get('cap_text')}; "
             f"CH2 {meta.get('ch2')}; encode self-check {meta.get('enc')}"]
    for (div, core), r in sorted(res.items()):
        lines.append(f"  divider {div:3d} CH2 {core}: >= +{THRESH} codes {r['bad']} of {r['n']} ({r['pct']:.3f} %); "
                     f"at codes 0x{HOT[0]:02X}..0x{HOT[1]:02X} {r['hot_bad']} of {r['hot_n']} ({r['hot_pct']:.2f} %)")
        bands = [f"{BAND * k:02X}:{int(r['band_n'][k])}/{100 * r['band_b'][k] / r['band_n'][k]:.2f}"
                 for k in range(len(r['band_n'])) if r['band_n'][k] > MIN_BAND_N]
        lines.append("      per band (start: samples/error %): " + "  ".join(bands))
    return "\n".join(lines)


def compare(runs: list[tuple[str, dict, dict]]) -> str:
    """One line per (divider, core) with every run's overall and hot-zone share; warnings for unlike runs."""
    lines = []
    ref = runs[0][2]
    for name, _, meta in runs[1:]:
        diff = [k for k in COMPARE_KEYS if meta.get(k) != ref.get(k)]
        if diff:
            lines.append(f"WARNING: {name} differs from {runs[0][0]} in {', '.join(diff)}: not like for like")
    keys = sorted({k for _, res, _ in runs for k in res})
    head = "  ".join(f"{meta.get('image') or name:>18s}" for name, _, meta in runs)
    lines.append(f"{'divider core':14s}  {head}   (overall % / hot-zone %)")
    for k in keys:
        cells = []
        for _, res, _ in runs:
            r = res.get(k)
            cells.append(f"{r['pct']:8.3f} /{r['hot_pct']:7.2f}" if r else f"{'-':>18s}")
        lines.append(f"{k[0]:7d} {k[1]:>6s}  " + "  ".join(cells))
    return "\n".join(lines)


def load(path: str) -> tuple[dict, dict]:
    z = np.load(path, allow_pickle=False)
    meta = json.loads(str(z["meta"]))
    raw = {}
    for k in z.files:
        if k.startswith("d") and k[-2:] in ("_A", "_B"):
            raw[(int(k[1:-2]), k[-1])] = z[k]
    return raw, meta


# ----------------------------------------------------------------------------------------------------------------------
# Bench

def run(args) -> int:
    from fnirsi1013d import protocol as P
    from fnirsi1013d.scope import Scope

    sha = args.image_sha256.lower()
    if len(sha) != 64 or any(c not in "0123456789abcdef" for c in sha):
        print("--image-sha256: the 64-digit sha256 of the image the flash step wrote and verified")
        return 2
    dividers = [int(d) for d in args.dividers.split(",")]
    counts = [int(c) for c in args.captures.split(",")]
    if len(counts) == 1:
        counts *= len(dividers)
    if len(counts) != len(dividers):
        print("--captures: one count, or one per divider")
        return 2
    s = Scope.connect(args.port, max_response=16384)
    raw: dict = {}
    try:
        s.ensure_control(watchdog_s=120)
        info = s.info
        h = s.health()
        ch2 = s.get_channel("CH2", cached=False)
        tb0, dec0, trig = s.get_timebase(cached=False), s.get_decim(), s.get_trigger(cached=False)
        meta = {
            "time": time.strftime("%Y-%m-%d %H:%M:%S"), "image": args.image, "image_sha256": sha,
            "image_label": KNOWN_IMAGES.get(sha, "experiment image (README section 19)"),
            "fw": info.fw_version.rstrip(b"\0").decode(errors="replace"), "fpga_version": info.fpga_version,
            "cap_value": h.cap_value if h is not None else None,
            "cap_text": P.cap_stages_text(h.cap_value) if h is not None and h.cap_value else None,
            "enc": ({"bad": h.enc_bad, "checked": h.enc_checked, "last33": h.enc_last33} if h is not None else None),
            "ch2": {"range": ch2.range, "hw_range": ch2.hw_range, "coupling": ch2.coupling, "dcoffset": ch2.dcoffset,
                    "offset_pwm": ch2.offset_pwm},
            "gen": {"freq_hz": args.gen_hz, "duty": 0.5} if args.gen_hz else None,
            "trigger_mode": trig.mode, "dividers": dividers, "captures": counts, "note": args.note,
        }
        print(f"{args.image}: fw {meta['fw']} FPGA 0x{info.fpga_version:04X}, stages {meta['cap_text']}, "
              f"CH2 {meta['ch2']}, encode self-check {meta['enc']}", flush=True)
        if info.fpga_version != 0x153D:
            print("WARNING: the FPGA does not report 0x153D (the experiment images keep the release's version word)")
        try:
            s.set_decim(0)
            if args.gen_hz:
                s.set_gen(enable=True, freq_hz=args.gen_hz, duty=0.5)
            for div, n in zip(dividers, counts):
                s.set_timebase(divider=div)
                time.sleep(0.2)
                A, B, tries = [], [], 0
                while len(A) < n:
                    tries += 1
                    if tries > 3 * n:
                        raise RuntimeError(f"divider {div}: no trigger in {tries - 1} tries")
                    cap = s.capture(("CH2",), timeout_s=args.timeout, keep_raw=True)
                    if cap is None:
                        continue
                    if cap.divider != div or cap.decim is not None:
                        raise RuntimeError(f"capture at divider {cap.divider}, decim {cap.decim}: expected divider "
                                           f"{div} with Filter Off")
                    c = cap.channels["CH2"]
                    A.append(np.asarray(c.raw_a, dtype=np.uint8))
                    B.append(np.asarray(c.raw_b, dtype=np.uint8))
                m = min(len(x) for x in A + B)
                raw[(div, "A")] = np.stack([x[:m] for x in A])
                raw[(div, "B")] = np.stack([x[:m] for x in B])
                print(f"  divider {div}: {n} captures of {m} words per core", flush=True)
            h2 = s.health()
            if h2 is not None:
                meta["enc_after"] = {"bad": h2.enc_bad, "checked": h2.enc_checked, "last33": h2.enc_last33}
                if h2.cap_value != meta["cap_value"]:
                    print(f"WARNING: the capture stages changed during the run (0x{h2.cap_value:04X}): the guard "
                          "moved a core")
                    meta["cap_value_after"] = h2.cap_value
        finally:
            s.set_timebase(divider=tb0.divider)
            s.set_decim(dec0.filter, dec0.flags)
            s.end_control()
    finally:
        s.close()
    res = analyse(raw)
    print(report(res, meta))
    if args.out:
        np.savez_compressed(args.out, meta=np.array(json.dumps(meta)),
                            **{f"d{div}_{core}": v for (div, core), v in raw.items()})
        print(f"saved {args.out}")
    return 0


# ----------------------------------------------------------------------------------------------------------------------
# Self-test

def synth(rng, n_caps: int, words: int, err: float, codes: tuple[int, int]) -> np.ndarray:
    """A 1 kHz-like square between two codes with 1-code noise; err: share of samples at medians 0x5E..0x6F made 5
    codes high (the class (b) signature)."""
    out = []
    for _ in range(n_caps):
        x = np.where((np.arange(words) // 700) % 2 == 0, codes[0], codes[1]).astype(float)
        x += rng.normal(0, 0.6, words)
        x = np.clip(np.round(x), 0, 255).astype(np.int16)
        hot = (x >= HOT[0]) & (x <= HOT[1])
        hit = hot & (rng.random(words) < err)
        hit[[0, -1]] = False
        hit[1:] &= ~hit[:-1]                    # isolated, so the 3-sample median stays on the signal
        x[hit] += 5
        out.append(np.clip(x, 0, 255).astype(np.uint8))
    return np.stack(out)


def selftest() -> int:
    rng = np.random.default_rng(1)
    raw = {(0, "A"): synth(rng, 8, 3000, 0.06, (0x60, 0xA8)), (0, "B"): synth(rng, 8, 3000, 0.0, (0x60, 0xA8))}
    res = analyse(raw)
    a, b = res[(0, "A")], res[(0, "B")]
    meta = {"image": "selftest", "fpga_version": 0x153D, "ch2": {"range": 3}, "cap_value": 0x06E3}
    print(report(res, meta))
    ok = 2.0 < a["hot_pct"] < 6.5 and b["pct"] < 0.05 and a["band_b"][0x60 // BAND] > 0
    other = dict(meta, image="other", ch2={"range": 4})
    txt = compare([("a", res, meta), ("b", res, other)])
    print(txt)
    ok &= "not like for like" in txt
    print("selftest:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", default="COM10")
    ap.add_argument("--image", help="label of the flashed image (README section 19: rel, enc-250, enc0, enc+250, ...)")
    ap.add_argument("--image-sha256", help="sha256 of the flashed image (the flash step's verified read-back)")
    ap.add_argument("--dividers", default="0,99")
    ap.add_argument("--captures", default="32,16", help="captures per divider (each gives both cores)")
    ap.add_argument("--gen-hz", type=float, default=1000.0, help="the calibration post's square (0: leave it)")
    ap.add_argument("--timeout", type=float, default=1.0)
    ap.add_argument("--note", default="", help="free text stored with the run (probe positions, room, ...)")
    ap.add_argument("--out", help="save the raw captures and the settings (.npz)")
    ap.add_argument("--analyse", nargs="+", metavar="NPZ", help="reprint saved runs; two or more: compare them")
    ap.add_argument("--selftest", action="store_true")
    a = ap.parse_args()
    if a.selftest:
        return selftest()
    if a.analyse:
        runs = []
        for p in a.analyse:
            raw, meta = load(p)
            res = analyse(raw)
            print(f"== {p}")
            print(report(res, meta))
            runs.append((p, res, meta))
        if len(runs) > 1:
            print(compare(runs))
        return 0
    if not a.image or not a.image_sha256:
        ap.error("a bench run needs --image and --image-sha256")
    return run(a)


if __name__ == "__main__":
    sys.exit(main())
