#!/usr/bin/env python3
"""ADC capture margins of a placed AL3 build (0x153A and later), from TD's own numbers and the AD9288 datasheet.

Usage (in WSL):  python3 -I capture_margins.py <io.timing> [tV_min tPD_max board_ns]

<io.timing> is a TD timing report of the placed design with the I/O references of `tb/capture_io.sdc` appended to
zaklad.sdc (see fpga/al3/README.md "Changes" section 10: import_device, import_db FPGA_pr.db, read_sdc,
report_timing -ep_num 2000 -path_num 1). It gives, per pin:
  - encode outputs: the pad arrival of each o_adc*_enc* relative to the clk_200MHz rising edge before the launching
    clk_ADC falling edge (launch at 3.75 ns; the write clock rises at 2.5 ns, so the pad switches at R + arrival - 2.5);
  - ADC data inputs: the setup and hold points of each pin's IOB DDR register (pad time relative to its clock edge).
AD9288 (datasheet rev. C, table 1, VD = VDD = 3.0 V, full temperature): data of a core is valid from tPD max (6 ns) after
its encode rising edge to tV min (2 ns) after the next one. Board: extra delay added to the encode trace plus the data
trace (default 0.3 ns; both short on this board).

Prints, per core and capture stage k (the pins as sampled 5 + 2.5 k ns before the write clock edge R), the margins to
the start and the end of the core's valid window at 200 MSa/s (T = 10 ns; slower rates only widen the window), the
worst bit of the core, and marks the best stage. Exit code 1 if the power-on stage of a core has less than 1 ns.
"""
import re
import sys

POWER_ON = {"1A": 2, "1B": 4, "2A": 1, "2B": 4}          # must match cap_sel's power-on value in zaklad.v
# Stages that keep the word's association (the B sample encoded 5 ns, half a word, before the A sample): A 1..2, B 3..4.
# The others take a sample one word newer or older (shown for information, in brackets).
ALLOWED = {"1A": (1, 2), "1B": (3, 4), "2A": (1, 2), "2B": (3, 4)}
CORES = [("1A", "i_adc1A_d", "o_adc1_encA", 1), ("1B", "i_adc1B_d", "o_adc1_encB", 0),
         ("2A", "i_adc2A_d", "o_adc2_encA", 1), ("2B", "i_adc2B_d", "o_adc2_encB", 0)]   # 1 = encoded on the fall


def blocks(txt):
    return re.split(r'\n(?=\s*Slack \((?:setup|hold) check\))', txt)


def main():
    txt = open(sys.argv[1]).read()
    tv = float(sys.argv[2]) if len(sys.argv) > 2 else 2.0
    tpd = float(sys.argv[3]) if len(sys.argv) > 3 else 6.0
    board = float(sys.argv[4]) if len(sys.argv) > 4 else 0.3
    enc = {}
    din = {}                                             # pin -> [setup point, hold point] relative to the edge
    for b in blocks(txt):
        m = re.search(r'Slack \((setup|hold) check\)', b)
        if not m:
            continue
        kind = m.group(1)
        sp = re.search(r'StartPoint:\s+(\S+)', b)
        ep = re.search(r'EndPoint:\s+(\S+)', b)
        if not sp or not ep:
            continue
        e = ep.group(1)
        if e.startswith('o_adc') and kind == 'setup':
            arr = float(re.search(r'Arrival time\s+([\d.]+)', b).group(1))
            enc.setdefault(e, arr)
        if 'iddr' in e:
            pin = sp.group(1)
            d = float(re.search(r'path2reg\s+([\d.]+)', b).group(1))
            ck = re.search(r'\$iddr_IN\.clk\s+([\d.]+)', b)
            cell = re.search(r'cell (setup|hold)\s+(-?[\d.]+)', b)
            if not ck or not cell:
                continue
            pt = float(ck.group(1)) + float(cell.group(2)) - d
            v = din.setdefault(pin, [None, None])
            if kind == 'setup' and v[0] is None:
                v[0] = pt                                # the pin must be stable from edge + pt ...
            if kind == 'hold' and v[1] is None:
                v[1] = pt                                # ... until edge + pt
    if len(enc) < 4 or len(din) < 32:
        print("capture_margins: report incomplete (%d encode pads, %d data pins)" % (len(enc), len(din)))
        return 2
    print("encode pads (ns after the write clock edge R): " +
          ", ".join("%s %.2f" % (k, v - 2.5) for k, v in sorted(enc.items())))
    print("AD9288 tV %.1f ns, tPD %.1f ns, board %.1f ns; window = [encode + tPD, next encode + tV]" % (tv, tpd, board))
    bad = 0
    for core, pin, encpin, fall in CORES:
        e = enc[encpin] - 2.5 + board + (5.0 if fall else 0.0)   # encode pad rise, ns after R (B: R, A: the fall)
        ap0 = min(din["%s[%d]" % (pin, i)][0] for i in range(8))
        ap1 = max(din["%s[%d]" % (pin, i)][1] for i in range(8))
        row = []
        best = None
        for k in range(6):
            t = -5.0 - 2.5 * k                           # pad sampling instant of stage k, relative to R
            lo, hi = t + ap0, t + ap1                    # the pin must be stable over [lo, hi]
            m = None
            for n in range(-4, 2):                       # encodes 10 ns apart
                ws, we = e + 10 * n + tpd, e + 10 * (n + 1) + tv
                if ws <= lo and hi <= we:
                    m = (lo - ws, we - hi)
            row.append(m)
            if m and k in ALLOWED[core] and (best is None or min(m) > min(row[best])):
                best = k
        print("  core %s (encode %.2f ns after R%s): " % (core, e - (5.0 if fall else 0.0), ", falling write clock" if fall else "")
              + "  ".join(("k%d %s" if k in ALLOWED[core] else "[k%d %s]")
                          % (k, ("%.2f/%.2f%s" % (m[0], m[1], "*" if k == best else "")) if m else "  --  ")
                          for k, m in enumerate(row)))
        p = row[POWER_ON[core]]
        if not p or min(p) < 1.0:
            bad += 1
            print("    power-on stage %d: margin %s < 1 ns" % (POWER_ON[core], "%.2f" % min(p) if p else "none"))
    print("margins: start/end of the valid window in ns, worst bit; * best stage" + ("" if not bad else "; %d cores below 1 ns" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
