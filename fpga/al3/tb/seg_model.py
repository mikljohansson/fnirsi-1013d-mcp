#!/usr/bin/env python3
"""Reference model for 0x1536 segmented capture and hardware averaging (fpga/al3/src/zaklad.v).

Reads the log of tb_seg.v and checks every logged run.

Averaging runs (mode 2): each record is logged from the sample memory when the post-pass starts
(position, accumulator base, words). The model accumulates them per position as specified in
docs/plans/segmented-capture.md section 4:
  - legacy: four 16-bit lanes CH1A, CH1B, CH2A, CH2B, each clamping at 65535;
  - 16-bit: two 32-bit lanes CH1, CH2;
  - the first record of each position is written, not added.
It then forms the MCU view for every logged readout (shift S, raw flag) and compares it word by word:
  - legacy: lane >> S;
  - 16-bit: min(65535, lane >> S) in L0 / L2;
  - raw: the stored word.
It also checks the record count per position against N.

Segment runs (mode 1):
  - Remap: every logged memory write must go to slot(k) * L + (virtual & (L - 1)), where slot(k) is the
    segment index k, wrapping at nseg in circular mode. Replaying the writes must reproduce the dumped
    slot memory.
  - Table: each entry read over the bus must equal {valid, triggered, trigger_address & (L - 1),
    timestamp} of the last segment in that slot. The slot about to be refilled when a circular or
    stopped run ended must read invalid.
  - Independent of the RTL, for every EXT-triggered segment:
    - the triggering edge is the first injected edge after the re-arm instant;
    - timestamp x 5 ns - (edge time - first write clock after the release) lies in [0, word + 35 ns];
    - the trigger offset is the write address at the edge, -1 .. +2 words;
    - the re-arm instant is at least the hold-off after the previous record's last word;
    - the record window was written completely inside its own segment and not overwritten by the
      slot's wrap.
  - Timestamps are multiples of the per-edge increment and increase with k.

Usage: seg_model.py check <seg.log>
"""
import sys


def parse(path):
    runs = {}
    order = []
    cur = None
    with open(path) as f:
        for line in f:
            t = line.split()
            if not t:
                continue
            tag = t[0]
            if tag == "END":
                break
            if tag == "RUN":
                rid = int(t[1])
                cur = {"id": rid, "mode": int(t[2]), "fmt16": int(t[3]), "count": int(t[4]), "n": int(t[5]),
                       "total": int(t[6]), "pre": int(t[7]), "m": int(t[8]), "circ": int(t[9]),
                       "word_ns": int(t[10]), "inc": int(t[11]), "hold": int(t[12]) if len(t) > 12 else 0,
                       "Z": None, "B": [], "W": [], "X": [], "H": [], "D": [], "E": [], "S": None, "T": {},
                       "M": {}, "R": [], "O": []}
                runs[rid] = cur
                order.append(rid)
                continue
            rid = int(t[1])
            if rid not in runs:
                continue
            r = runs[rid]
            if tag == "Z":
                if r["Z"] is None:
                    r["Z"] = int(t[2])
            elif tag == "B":
                r["B"].append(int(t[2]))
            elif tag == "W":
                r["W"].append((int(t[2]), int(t[3]), int(t[4], 16), int(t[5])))
            elif tag == "X":
                r["X"].append((int(t[2]), int(t[3])))
            elif tag == "H":
                r["H"].append(int(t[2]))
            elif tag == "D":
                r["D"].append(int(t[2]))
            elif tag == "E":
                r["E"].append((int(t[2]), int(t[3]), int(t[4]), int(t[5])))
            elif tag == "S":
                r["S"] = (int(t[2], 16), int(t[3]), int(t[4]), int(t[5]))
            elif tag == "T":
                r["T"][int(t[2])] = int(t[3], 16)
            elif tag == "M":
                r["M"][int(t[2])] = int(t[3], 16)
            elif tag == "R":
                r["R"].append((int(t[2]), int(t[3]), int(t[4]), [int(v, 16) for v in t[5:]]))
            elif tag == "O":
                r["O"].append((int(t[2]), int(t[3]), int(t[4]), int(t[5], 16)))
    return [runs[i] for i in order]


def check_avg(r, fails, stats):
    # re-arm time after each triggered record: the record end (D) is followed by the post-pass
    for k in range(1, len(r["H"])):
        if k - 1 < len(r["D"]) and r["H"][k] > r["D"][k - 1]:
            stats.setdefault("dead_avg", []).append((r["word_ns"], r["pre"], r["total"],
                                                     (r["H"][k] - r["D"][k - 1]) / 1000.0))
    acc = {}
    seen = {}
    for pos, base, start, words in r["R"]:
        if len(words) != r["total"]:
            fails.append("run %d: record of %d words, total %d" % (r["id"], len(words), r["total"]))
        first = seen.get(pos, 0) == 0
        seen[pos] = seen.get(pos, 0) + 1
        if base != pos * r["total"]:
            fails.append("run %d: position %d at accumulator base %d" % (r["id"], pos, base))
        for j, w in enumerate(words):
            a = acc.get(base + j, [0, 0, 0, 0]) if not first else [0, 0, 0, 0]
            if r["fmt16"]:
                a = [a[0] + (w & 0xFFFF), 0, a[2] + (w >> 16), 0]
            else:
                a = [min(65535, a[i] + ((w >> (8 * i)) & 0xFF)) for i in range(4)]
            acc[base + j] = a
    npos = r["count"]
    stopped = r["S"] is not None and r["S"][1] < npos * r["n"]
    for p in range(npos):
        if not stopped and seen.get(p, 0) != r["n"]:
            fails.append("run %d: position %d got %d records, N = %d" % (r["id"], p, seen.get(p, 0), r["n"]))
    if r["S"] is not None and r["S"][1] != len(r["R"]):
        fails.append("run %d: status count %d, %d records logged" % (r["id"], r["S"][1], len(r["R"])))
    words = 0
    for sh, raw, i, got in r["O"]:
        if i >= npos * r["total"] or i not in acc:
            continue
        a = acc[i]
        if raw:
            exp = (a[0] | (a[2] << 32)) if r["fmt16"] else (a[0] | (a[1] << 16) | (a[2] << 32) | (a[3] << 48))
        elif r["fmt16"]:
            exp = min(65535, a[0] >> sh) | (min(65535, a[2] >> sh) << 32)
        else:
            exp = 0
            for k in range(4):
                exp |= ((a[k] >> sh) if sh < 16 else 0) << (16 * k)
        words += 1
        if got != exp:
            if len(fails) < 20:
                fails.append("run %d: word %d S %d raw %d: got %016x expected %016x" % (r["id"], i, sh, raw, got, exp))
            else:
                fails.append("...")
    return "averaging: %d records, %d readout words checked" % (len(r["R"]), words)


def check_seg(r, fails, stats):
    rid = r["id"]
    L = 1 << r["m"]
    mask = L - 1
    nseg = r["count"]
    # segment k starts at the arm (writes before the first restart) or at its restart edge
    starts = [r["Z"] if r["Z"] is not None else 0] + r["B"]
    seg_of_w = []
    k = 0
    bi = 0
    mem = {}
    vmax = {}
    for v, ph, w, t in r["W"]:
        while bi < len(r["B"]) and t > r["B"][bi]:
            bi += 1
        k = bi
        slot = k % nseg if r["circ"] else k
        exp_ph = (slot * L + (v & mask)) & 8191
        if ph != exp_ph:
            fails.append("run %d: segment %d write v %d to %d, expected %d" % (rid, k, v, ph, exp_ph))
        mem[ph] = w
        seg_of_w.append(k)
        vmax[k] = max(vmax.get(k, -1), v)
        if len(fails) > 20:
            return "aborted"
    cnt = r["S"][1] if r["S"] else len(r["E"])
    refill = (cnt % nseg) if (r["circ"] or cnt < nseg) else -1   # the abandoned slot keeps being written
    for ph, w in r["M"].items():
        if ph // L == refill:
            continue
        if ph in mem and mem[ph] != w:
            fails.append("run %d: memory word %d = %08x, replay %08x" % (rid, ph, w, mem[ph]))
            if len(fails) > 20:
                return "aborted"
    # table against the sequencer's own entry fields, slot by slot
    if cnt != len(r["E"]):
        fails.append("run %d: status count %d, %d segment ends logged" % (rid, cnt, len(r["E"])))
    expect = {}
    for k, (idx, ta, trig, ts) in enumerate(r["E"]):
        slot = k % nseg if r["circ"] else k
        if idx != slot:
            fails.append("run %d: segment %d written to entry %d, expected %d" % (rid, k, idx, slot))
        expect[slot] = (1 << 63) | (trig << 53) | ((ta & mask) << 40) | ts
    run_continued = r["circ"] or cnt < nseg
    if run_continued and r["B"]:
        expect[cnt % nseg] = 0
    for s, e in expect.items():
        got = r["T"].get(s)
        if got is not None and got != e:
            fails.append("run %d: table slot %d = %016x, expected %016x" % (rid, s, got, e))
    # independent checks per segment
    prev_ts = -1
    tsl = []
    for k, (idx, ta, trig, ts) in enumerate(r["E"]):
        if ts % r["inc"] != 0:
            fails.append("run %d: segment %d timestamp %d not a multiple of %d" % (rid, k, ts, r["inc"]))
        if ts <= prev_ts:
            fails.append("run %d: segment %d timestamp %d not after %d" % (rid, k, ts, prev_ts))
        prev_ts = ts
        if k < len(r["H"]) and k >= 1 and k - 1 < len(r["D"]):
            dead = (r["H"][k] - r["D"][k - 1]) / 1000.0
            stats.setdefault("dead", []).append((r["word_ns"], r["pre"], r["hold"], dead))
            if dead < r["hold"] * 5.0:
                fails.append("run %d: segment %d re-armed %.1f ns after the previous end, hold-off %d ns" %
                             (rid, k, dead, r["hold"] * 5))
        if not trig:
            if (ta & mask) != (r["pre"] & mask):
                fails.append("run %d: untriggered segment %d offset %d, pre %d" % (rid, k, ta & mask, r["pre"]))
            continue
        # window: written inside segment k, not overwritten by the slot wrap
        lo = ta - r["pre"]
        hi = lo + r["total"] - 1
        over = vmax.get(k, -1) - hi
        stats.setdefault("over", []).append(over)
        if lo < 0 or vmax.get(k, -1) < hi or vmax.get(k, -1) - lo >= L:
            fails.append("run %d: segment %d window %d..%d, last written %d, slot %d words" %
                         (rid, k, lo, hi, vmax.get(k, -1), L))
        if not r["X"] or k >= len(r["H"]):
            continue
        arm = r["H"][k]
        cand = [x for x in r["X"] if x[0] + 25000 > arm]
        if not cand:
            fails.append("run %d: segment %d has no EXT edge after its re-arm" % (rid, k))
            continue
        te, vw = cand[0]
        if abs(te + 22000 - arm) < 8000:
            continue                                # edge within the latency of the re-arm: ambiguous
        dts = ts * 5.0 - (te - r["Z"]) / 1000.0
        tsl.append(dts)
        if dts < 0 or dts > r["word_ns"] + 35:
            fails.append("run %d: segment %d timestamp %.1f ns after its EXT edge (word %d ns)" %
                         (rid, k, dts, r["word_ns"]))
        d = ((ta & mask) - ((vw - 1) & mask)) & mask
        if d >= L // 2:
            d -= L
        stats.setdefault("doff", []).append(d)
        dmax = 1 + -(-30 // r["word_ns"])            # EXT pad-to-trigger latency 20-25 ns plus one edge
        if d < -1 or d > dmax:
            fails.append("run %d: segment %d offset %d, write address at the edge %d" % (rid, k, ta & mask, vw))
    if tsl:
        stats.setdefault("ts", []).append((r["word_ns"], min(tsl), max(tsl)))
    return "segments: %d ends, %d writes, %d table entries, %d slot words compared" % (
        len(r["E"]), len(r["W"]), len(r["T"]), len(r["M"]))


def check(path):
    runs = parse(path)
    fails = []
    stats = {}
    for r in runs:
        nf = len(fails)
        if r["mode"] == 2:
            msg = check_avg(r, fails, stats)
        else:
            msg = check_seg(r, fails, stats)
        print("run %2d mode %d fmt16 %d count %d n %d total %d: %s%s" % (
            r["id"], r["mode"], r["fmt16"], r["count"], r["n"], r["total"], msg,
            "" if len(fails) == nf else "  <-- %d failures" % (len(fails) - nf)))
    if "dead" in stats:
        by = {}
        for word, pre, hold, dead in stats["dead"]:
            by.setdefault((word, pre, hold), []).append(dead)
        for key in sorted(by):
            v = by[key]
            print("re-arm time (last word to trigger enabled), word %d ns, pre %d, hold-off %d x 5 ns: %.1f .. %.1f ns"
                  % (key[0], key[1], key[2], min(v), max(v)))
    if "dead_avg" in stats:
        by = {}
        for word, pre, total, dead in stats["dead_avg"]:
            by.setdefault((word, pre, total), []).append(dead)
        for key in sorted(by):
            v = sorted(by[key])
            print("averaging re-arm time, word %d ns, pre %d, total %d: %.1f .. %.1f ns (post-pass %d x 5 ns)"
                  % (key[0], key[1], key[2], v[0], v[len(v) // 2], key[2]))
    if "ts" in stats:
        for word, lo, hi in stats["ts"]:
            print("timestamp - EXT edge, word %d ns: %.1f .. %.1f ns" % (word, lo, hi))
    if "doff" in stats:
        print("trigger offset - (write address at the edge - 1): %d .. %d words" % (min(stats["doff"]), max(stats["doff"])))
    if "over" in stats:
        print("words written after the record window: %d .. %d" % (min(stats["over"]), max(stats["over"])))
    for f in fails[:25]:
        print("FAIL", f)
    print("seg_model: %d runs, %d failures -> %s" % (len(runs), len(fails), "FAIL" if fails or not runs else "PASS"))
    return 0 if not fails and runs else 1


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "check":
        sys.exit(check(sys.argv[2]))
    print(__doc__)
    sys.exit(2)
