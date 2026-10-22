#!/usr/bin/env python3
"""ADC capture margins of a placed AL3 build (0x153A and later), from TD's own numbers and the AD9288 datasheet.

Usage (in WSL):  python3 -I capture_margins.py <io.timing> [tV_min tPD_max board_ns] [--bits]

--bits also prints every data pin's setup / hold point and the per-core skew (fpga/al3/README.md section 18).

<io.timing> is a TD timing report of the placed design with the I/O references of `tb/capture_io.sdc` appended to
zaklad.sdc (tb/capture_report.sh: import_device, import_db FPGA_pr.db, read_sdc, report_timing -ep_num 2000
-path_num 1). It gives, per pin:
  - encode outputs: the pad arrival of each o_adc*_enc* relative to the clk_200MHz rising edge before the launching
    clk_ADC falling edge (launch at 3.75 ns; the write clock edge that the encode follows is at 2.5 ns, so the pad
    switches at E + arrival - 2.5 for either write clock edge E);
  - ADC data inputs: the setup and hold points of each pin's IOB DDR register (pad time relative to its clock edge).
AD9288 (datasheet rev. C, table 1, VD = VDD = 3.0 V, full temperature): data of a core is valid from tPD max (6 ns) after
its encode rising edge to tV min (2 ns) after the next one, so the pins are in transition for 4 ns per encode period T
and stable for T - 4 ns. Board: extra delay on the encode trace plus the data trace (default 0.3 ns; both are short).

0x153A (fpga/al3/README.md "Changes" section 10): core A is encoded on the falling write clock edge and captured against
it, core B on the rising edge and captured against that; stage k samples the pins 5 + 2.5 k ns before the core's own
edge. The window of the sample a stage sees therefore has the same position relative to that edge at every rate (its
start is encode + tPD - T, its end encode + tV, both relative to the edge), and the margins below hold for every divider.
With 0x63 bit 12 / 13 the B core is taken from the falling-edge path (ADC data-align mode: B data re-timed by the chip to
the A encode); those margins are printed as "B/A-ref" rows.

Prints, per core and stage, the margins to the start and the end of the valid window (worst bit of the core, pin
aperture included), marks the best stage of the allowed pair (3, 4; 2 and 5 keep the word association only when the
window has drifted towards them and are shown in brackets), and exits 1 if the power-on stage of a core has less than
1 ns. POWER_ON must match CAP_SEL_PO in zaklad.v.
"""
import re
import sys

POWER_ON = {"1A": 4, "1B": 4, "2A": 3, "2B": 4}          # CAP_SEL_PO = 16'h08E4: bits 2:0 1A, 5:3 1B, 8:6 2A, 11:9 2B
ALLOWED = (3, 4)
CORES = [("1A", "i_adc1A_d", "o_adc1_encA"), ("1B", "i_adc1B_d", "o_adc1_encB"),
         ("2A", "i_adc2A_d", "o_adc2_encA"), ("2B", "i_adc2B_d", "o_adc2_encB"),
         ("1B/A-ref", "i_adc1B_d", "o_adc1_encA"), ("2B/A-ref", "i_adc2B_d", "o_adc2_encA")]


def blocks(txt):
    return re.split(r'\n(?=\s*Slack \((?:setup|hold) check\))', txt)


def main():
    bits = "--bits" in sys.argv
    argv = [a for a in sys.argv if a != "--bits"]
    txt = open(argv[1]).read()
    tv = float(argv[2]) if len(argv) > 2 else 2.0
    tpd = float(argv[3]) if len(argv) > 3 else 6.0
    board = float(argv[4]) if len(argv) > 4 else 0.3
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
    print("encode pads (ns after their write clock edge; A: the falling edge, B: the rising edge): " +
          ", ".join("%s %.2f" % (k, v - 2.5) for k, v in sorted(enc.items())))
    print("AD9288 tV %.1f ns, tPD %.1f ns, board %.1f ns; window relative to the core's edge = "
          "[encode + tPD - T, encode + tV] at every T" % (tv, tpd, board))
    if bits:
        print("data pins: setup / hold point in ns relative to the IOB clock edge (the pin must be stable over "
              "[setup, hold]); per core the spread of the setup points")
        for core, pin, _ in CORES[:4]:
            pts = [din["%s[%d]" % (pin, i)] for i in range(8)]
            print("  core %-3s " % core + " ".join("d%d %+.2f/%+.2f" % (i, p[0], p[1]) for i, p in enumerate(pts))
                  + "  skew %.2f" % (max(p[0] for p in pts) - min(p[0] for p in pts)))
    bad = 0
    T = 10.0                                             # the margins are the same at every rate (see above)
    for core, pin, encpin in CORES:
        e = enc[encpin] - 2.5 + board                    # encode pad rise, ns after the core's edge
        ap0 = min(din["%s[%d]" % (pin, i)][0] for i in range(8))
        ap1 = max(din["%s[%d]" % (pin, i)][1] for i in range(8))
        row = []
        best = None
        for k in range(6):
            t = -5.0 - 2.5 * k                           # pad sampling instant of stage k, relative to the edge
            lo, hi = t + ap0, t + ap1                    # the pin must be stable over [lo, hi]
            m = None
            for n in range(-4, 2):                       # encodes T apart
                ws, we = e + T * n + tpd, e + T * (n + 1) + tv
                if ws <= lo and hi <= we:
                    m = (lo - ws, we - hi)
            row.append(m)
            if m and k in ALLOWED and (best is None or min(m) > min(row[best])):
                best = k
        print("  core %-8s (encode %.2f ns after its edge): " % (core, e)
              + "  ".join(("k%d %s" if k in ALLOWED else "[k%d %s]")
                          % (k, ("%.2f/%.2f%s" % (m[0], m[1], "*" if k == best else "")) if m else "  --  ")
                          for k, m in enumerate(row)))
        if core in POWER_ON:
            p = row[POWER_ON[core]]
            if not p or min(p) < 1.0:
                bad += 1
                print("    power-on stage %d: margin %s < 1 ns" % (POWER_ON[core], "%.2f" % min(p) if p else "none"))
            elif best is not None and best != POWER_ON[core]:
                print("    power-on stage %d has %.2f ns, stage %d would have %.2f" %
                      (POWER_ON[core], min(p), best, min(row[best])))
    print("margins: start/end of the valid window in ns, worst bit; * best allowed stage" +
          ("" if not bad else "; %d cores below 1 ns" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
