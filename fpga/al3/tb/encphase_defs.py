#!/usr/bin/env python3
"""iverilog defines for a design under test (fpga/al3/README.md sections 19 and 21; used by run.sh, encphase.sh, mcuread.sh).

    python3 -I encphase_defs.py <zaklad.v>            -> "" or "-DPLL_C2_NS=.. -DPLL_C3_NS=.. -DENC_B_NS=.. -DENC_A_NS=.."
    python3 -I encphase_defs.py --windows <zaklad.v>  -> "<window A> <window B>" in ns (encphase.sh's annotations)

A design without the encode ODDRs (0x153D and older, e.g. a SRC_REV / DUT of those; the references are never passed
here) needs no define: sim_models.v's pll gives clk1 at 1.25 ns and the encode flops switch on its falling edge, 1.25 ns
after the write clock edge; their D path (enc_nxt on the rising clk_200MHz edge) has a 3.75 ns window.

The committed source since 0x153E, and every image tools/encphase_variant.py makes from it (the fpga-encphase --iob
images before), encodes on the rising edges of PLL C2 (B) and C3 (A), read here from
<dir of zaklad.v>/al_ip/pll.v (t ns after C0 = CPHASE - 4 + FPHASE / 8, mod 5). In hardware the IOB output path is about
1.25 ns shorter than the fabric route of the release's encode flops (TD: 6.97 ns pad at t = 0 against the release's
6.92 .. 7.10 at 1.25), so the simulation places each clock 1.25 ns earlier than the hardware does: the encode pins then
switch where the release's do in simulation for the same pad time, and the testbenches' ADC models (pad delay after the
pin) stay calibrated. The D of those registers (enc_cur, loaded on the falling clk_200MHz edge) is stable over the whole
period after that edge, so the earlier edge loads the same value. ENC_B_NS / ENC_A_NS: the simulated pins' delay after
their write clock edge (tb_adccap.v's model). Window: from enc_cur's edge to the encode clock edge.
"""
import re
import sys
from pathlib import Path

IOB_ADVANCE = 1.25


def clock_ns(pll: str, n: int) -> float:
    c = int(re.search(r'\.CLKC%d_CPHASE\((\d+)\)' % n, pll).group(1))
    f = int(re.search(r'\.CLKC%d_FPHASE\("(\d)"\)' % n, pll).group(1))
    return (c - 4 + f / 8) % 5


def main() -> int:
    args = sys.argv[1:]
    windows = args[:1] == ['--windows']
    if windows:
        args = args[1:]
    dut = Path(args[0])
    src = dut.read_text(encoding='latin-1')
    if 'AL_LOGIC_ODDR enc_oddr_' not in src:
        print('3.75 3.75' if windows else '')
        return 0
    pll = (dut.parent / 'al_ip' / 'pll.v').read_text(encoding='latin-1')
    sim_b = (clock_ns(pll, 2) - IOB_ADVANCE) % 5
    sim_a = (clock_ns(pll, 3) - IOB_ADVANCE) % 5
    for v in (sim_b, sim_a):
        if not 2.6 <= v <= 4.9:
            sys.exit('encphase_defs: an encode clock at %.3f ns in simulation leaves (2.5, 5): the testbenches assume the '
                     'pins switch between the falling and the rising clk_200MHz edge' % v)
    if windows:
        print('%.3f %.3f' % (sim_a - 2.5, sim_b - 2.5))
    else:
        print('-DPLL_C2_NS=%.3f -DPLL_C3_NS=%.3f -DENC_B_NS=%.3f -DENC_A_NS=%.3f'
              % (sim_b, sim_a, sim_b - 2.5, sim_a - 2.5))
    return 0


if __name__ == '__main__':
    sys.exit(main())
