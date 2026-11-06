#!/usr/bin/env python3
"""ADC capture margins of a placed AL3 build (0x153F and later), from TD's own numbers and the AD9288 datasheet.

Usage (in WSL):  python3 -I capture_margins.py <io.timing> [tV_min tPD_max board_ns] [--bits]

--bits also prints every data pin's setup / hold point and the per-core skew (fpga/al3/README.md section 18).

<io.timing> is a TD timing report of the placed design with the I/O references of `tb/capture_io.sdc` appended to
zaklad.sdc (tb/capture_report.sh: import_device, import_db FPGA_pr.db, read_sdc, report_timing -ep_num 2000
-path_num 1). It gives, per pin:
  - encode outputs: the pad arrival of each o_adc*_enc* relative to the clk_200MHz rising edge before the launching
    encode clock edge (the write clock edge that the encode follows is at 2.5 ns, so the pad switches at
    E + arrival - 2.5 for either write clock edge E);
  - ADC data inputs: the setup and hold points of each pin's IOB DDR register (pad time relative to its clock edge).

Model (fpga/al3/README.md "Changes" section 22, the maintainer's unit on 2026-11-06): both output buses of a chip change
after the rising edge of its B encode, i.e. after the write clock's rising edge R, whatever the rate. With the
datasheet's figures taken from that edge (rev. C, table 1: tV min 2 ns, tPD max 6 ns) the pins of both cores are in
transition from encB pad + board + tV to encB pad + board + tPD after every R and stable for the rest of the period
T. The bench put the middle of that transition 10 .. 12.5 ns after R for core A; the defaults give 9.3 .. 13.3.

0x153F captures both cores of a chip at one instant:
  - full rate (T = 10 ns): the pins at R - 2.5 k ns for the chip's stage k = 2..5 (command 0x63). The stage is trained
    per unit; the rows show which stages the datasheet limits leave clean (a stage at or inside the transition is
    "--"; the unit's trained stage 3 lies just after the transition's typical end).
  - divided rates (T >= 20 ns): the pins at R + 2.5 ns, fixed. Its margins are printed for T = 20 ns (the tightest:
    back to the end of the previous release's transition, forward to the start of this R's) and the script exits 1 if
    either is under 1 ns: nothing trains this instant.
Margins are per core, worst bit, pin aperture included.
"""
import re
import sys

STAGES = (2, 3, 4, 5)                                    # 0x63: the chip's stage, R - 2.5 k ns at full rate
T_DIV = 2.5                                              # divided rates: the pins at R + 2.5 ns
CORES = [("1A", "i_adc1A_d", "o_adc1_encB"), ("1B", "i_adc1B_d", "o_adc1_encB"),
         ("2A", "i_adc2A_d", "o_adc2_encB"), ("2B", "i_adc2B_d", "o_adc2_encB")]


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
            launch = re.search(r'launch clock edge\s+clock\s+([\d.]+)', b)
            if launch and float(launch.group(1)) < 2.5:
                # fpga-encphase IOB images: an encode clock edge in [0, 2.5) ns loads the write clock value of the
                # edge at 2.5, so it is the edge of the next period (TD reports it one period early)
                arr += 5.0
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
    print("AD9288 tV %.1f ns, tPD %.1f ns, board %.1f ns; both buses of a chip in transition over "
          "[encB + tV, encB + tPD] after every rising write clock edge R" % (tv, tpd, board))
    if bits:
        print("data pins: setup / hold point in ns relative to the IOB clock edge (the pin must be stable over "
              "[setup, hold]); per core the spread of the setup points")
        for core, pin, _ in CORES[:4]:
            pts = [din["%s[%d]" % (pin, i)] for i in range(8)]
            print("  core %-3s " % core + " ".join("d%d %+.2f/%+.2f" % (i, p[0], p[1]) for i, p in enumerate(pts))
                  + "  skew %.2f" % (max(p[0] for p in pts) - min(p[0] for p in pts)))
    bad = 0

    def margins(t, e, ap0, ap1, T):
        """(ns after the end of the last transition, ns before the start of the next) for a capture at t after R, or
        None inside a transition; e = the release pad edge after R."""
        lo, hi = t + ap0, t + ap1                        # the pin must be stable over [lo, hi]
        for n in range(-6, 3):
            ws, we = e + T * n + tpd, e + T * (n + 1) + tv
            if ws <= lo and hi <= we:
                return lo - ws, we - hi
        return None

    for core, pin, encpin in CORES:
        e = enc[encpin] - 2.5 + board                    # the B encode's pad rise, ns after R
        ap0 = min(din["%s[%d]" % (pin, i)][0] for i in range(8))
        ap1 = max(din["%s[%d]" % (pin, i)][1] for i in range(8))
        row = [margins(-2.5 * k, e, ap0, ap1, 10.0) for k in STAGES]
        dv = margins(T_DIV, e, ap0, ap1, 20.0)
        print("  core %-2s (release %.2f ns after R): full rate " % (core, e)
              + "  ".join("k%d %s" % (k, ("%.2f/%.2f" % m) if m else "  --  ") for k, m in zip(STAGES, row))
              + "   divided (T 20) %s" % (("%.2f/%.2f" % dv) if dv else "--"))
        if not dv or min(dv) < 1.0:
            bad += 1
            print("    divided rates: margin %s < 1 ns" % ("%.2f" % min(dv) if dv else "none"))
    print("margins: after the last transition / before the next, ns, worst bit; full-rate stages are trained per unit" +
          ("" if not bad else "; %d cores below 1 ns at the divided rates" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
