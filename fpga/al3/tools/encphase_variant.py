#!/usr/bin/env python3
"""Make an encode-phase experiment source tree from fpga/al3/src (README sections 19 and 21).

Since 0x153E the committed source drives the four ADC encode pins from IOB output registers (AL_LOGIC_ODDR) on PLL C2
(clk_ADCB, the B encodes, 0 degrees) and C3 (clk_ADCA, the A encodes, 375 ps after C2): the recipe of the fpga-encphase
image enc0, adopted by the owner on 2026-11-04. An experiment image is this script applied to the source, so it is
reproducible from the commit that holds both (build it with tools/encphase_build.sh). The images keep the version word
0x153E; they are told apart by their sha256 only (tools/enc_ab.py --image-sha256).

    python3 -I fpga/al3/tools/encphase_variant.py OUT_DIR [--phase-ps N] [--skew-ps S] [--move SECTION ANCHOR]...

--phase-ps N   C2 and C3 moved together by N ps (a multiple of 125, the VCO's 1/8 step) from the source's C2 at 0 ps.
--skew-ps S    C3 (the A encodes) S ps after C2 (default 375, the source's; a multiple of 125). The parked "skew images"
               of README section 19 move one core only: A -250 / B 0 is --skew-ps 125, A 0 / B -250 is --phase-ps -250
               --skew-ps 625.
--move S A     a placement roll: move the zaklad.v section whose header starts with S before the one starting with A
               (order only; the knob that re-rolls TD 4.6.8's placement, README section 17 "Rolls").

With no option the tree equals src/. TD's PLL macro (arch/al3_macro.v) allows CLKCn_CPHASE 1..DIV (5 here); C0 = 0
degrees is CPHASE 4, so an output t ns after C0 is CPHASE 4 + floor(e / 8), FPHASE e mod 8 with e = 8 t taken in
-24..15 (mod 40). TD 4.6.8 accepts a CPHASE above DIV without a message and times it as asked; this script never writes
one. `encphase.txt` in the tree records the options and the clock times; tb/run.sh reads the clocks from al_ip/pll.v
(tb/encphase_defs.py), which refuses a phase whose simulated encode edges leave (2.5, 5) ns.
"""
from __future__ import annotations

import argparse
import re
import shutil
import sys
from pathlib import Path

SEP = '//-------------------------------------------------------------------------------------'

PLL_C0_CPHASE = 4          # pll.v: C0 (clk_200MHz) at 0 degrees; TD's macro: 1 <= CPHASE <= DIV (5)
SRC_B_NS = 0.0             # src/al_ip/pll.v: C2 (the B encodes) rises with C0, 2.5 ns after the write clock edge
SRC_SKEW_PS = 375          # src/al_ip/pll.v: C3 (the A encodes) 375 ps after C2


def pll_setting(t_ns: float) -> tuple[int, int]:
    """(CPHASE, FPHASE) of a PLL output rising t_ns after C0 (mod 5 ns), in the macro's legal range CPHASE 1..5."""
    e = round(t_ns * 8) % 40
    if e > 15:
        e -= 40                                    # -24 .. 15 eighths = CPHASE 1 .. 5
    return PLL_C0_CPHASE + e // 8, e % 8


def clock_times(phase_ps: int, skew_ps: int) -> tuple[float, float]:
    """Rising edges of C2 (B) and C3 (A) after C0, ns mod 5."""
    for v in (phase_ps, skew_ps):
        if v % 125:
            sys.exit('--phase-ps and --skew-ps must be multiples of 125 (the VCO\'s 1/8 step)')
    b = (SRC_B_NS + phase_ps / 1000) % 5
    return b, (b + skew_ps / 1000) % 5


def sub_once(s: str, pattern: str, new: str, what: str) -> str:
    out, n = re.subn(pattern, new, s)
    if n != 1:
        sys.exit(f'{what}: expected the pattern once, found it {n} times (has src/ changed?)')
    return out


def apply_pll(pll: str, tb: float, ta: float, label: str) -> str:
    """C2 (clk_ADCB) to tb, C3 (clk_ADCA) to ta; the header lines say so."""
    for n, t, core in ((2, tb, 'B'), (3, ta, 'A')):
        c, f = pll_setting(t)
        pll = sub_once(pll, rf'\.CLKC{n}_CPHASE\(\d+\)', f'.CLKC{n}_CPHASE({c})', f'pll.v CLKC{n}_CPHASE')
        pll = sub_once(pll, rf'\.CLKC{n}_FPHASE\("\d"\)', f'.CLKC{n}_FPHASE("{f}")', f'pll.v CLKC{n}_FPHASE')
        pll = sub_once(pll, rf'(//\t\tC{n} +\t\| 200\.000000MHZ\t\| )[^\n]*',
                       r'\g<1>' + f'{t / 5 * 360:.4f} DEG  ({label}: the {core} encodes)', f'pll.v C{n} header')
    return pll


def move_section(text: str, sec: str, anc: str) -> str:
    lines = text.split('\n')
    starts = [i for i, ln in enumerate(lines) if ln.rstrip('\r') == SEP]

    def hits(ls, st, prefix):
        return [k for k, i in enumerate(st) if i + 1 < len(ls) and ls[i + 1].startswith(prefix)]

    h = hits(lines, starts, sec)
    if len(h) != 1:
        sys.exit(f'--move: section {sec!r} matches {len(h)}')
    if h[0] + 1 >= len(starts):
        sys.exit(f'--move: section {sec!r} is the last one')
    a, b = starts[h[0]], starts[h[0] + 1]
    block, rest = lines[a:b], lines[:a] + lines[b:]
    starts2 = [i for i, ln in enumerate(rest) if ln.rstrip('\r') == SEP]
    h2 = [starts2[k] for k in hits(rest, starts2, anc)]
    if len(h2) != 1:
        sys.exit(f'--move: anchor {anc!r} matches {len(h2)}')
    return '\n'.join(rest[:h2[0]] + block + rest[h2[0]:])


def rd(p: Path) -> str:
    with open(p, encoding='latin-1', newline='') as f:
        return f.read()


def wr(p: Path, s: str) -> None:
    with open(p, 'w', encoding='latin-1', newline='') as f:
        f.write(s)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('out', type=Path, help='output src directory (created; must not exist)')
    ap.add_argument('--src', type=Path, default=Path(__file__).resolve().parent.parent / 'src')
    ap.add_argument('--phase-ps', type=int, default=0)
    ap.add_argument('--skew-ps', type=int, default=SRC_SKEW_PS, help='A encodes this much after B (default 375)')
    ap.add_argument('--move', nargs=2, action='append', default=[], metavar=('SECTION', 'ANCHOR'))
    a = ap.parse_args()
    if a.out.exists():
        sys.exit(f'{a.out} exists')
    zsrc = rd(a.src / 'zaklad.v')
    if 'AL_LOGIC_ODDR enc_oddr_' not in zsrc:
        sys.exit(f'{a.src}/zaklad.v has no encode ODDRs: a source before 0x153E (use that commit\'s own tool)')
    shutil.copytree(a.src, a.out)
    zp, pllp = a.out / 'zaklad.v', a.out / 'al_ip' / 'pll.v'
    z, pll = zsrc, rd(pllp)
    tb, ta = clock_times(a.phase_ps, a.skew_ps)
    if a.phase_ps or a.skew_ps != SRC_SKEW_PS:
        pll = apply_pll(pll, tb, ta, f'encphase {a.phase_ps:+d} ps skew {a.skew_ps} ps')
    for sec, anc in a.move:
        z = move_section(z, sec, anc)
    wr(zp, z)
    wr(pllp, pll)
    moves = ';'.join(s + '>' + t for s, t in a.move) or '-'
    desc = f'phase_ps={a.phase_ps} skew_ps={a.skew_ps} B_NS={tb:.3f} A_NS={ta:.3f} moves={moves}\n'
    (a.out / 'encphase.txt').write_text(desc, encoding='ascii')
    print(desc, end='')


if __name__ == '__main__':
    main()
