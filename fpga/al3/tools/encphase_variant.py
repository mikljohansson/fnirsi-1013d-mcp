#!/usr/bin/env python3
"""Make an encode-phase experiment source tree from fpga/al3/src (roadmap section 6 item 4, knob 2; README section 19).

The release source (0x153D, the gated encodes) stays as it is; every experiment image is this script applied to it, so an
image is reproducible from the commit that holds the script and the source (build it with tools/encphase_build.sh).

    python3 -I fpga/al3/tools/encphase_variant.py OUT_DIR [--iob [--skew-ps S]] [--phase-ps N] [--move SECTION ANCHOR]

--iob          each encode pin from an IOB output register (AL_LOGIC_ODDR, d1 = d2): B on the rising edge of a new
               clk_ADCB (PLL C2), A on the rising edge of a new clk_ADCA (PLL C3); C1 (clk_ADC) is left unused, so the
               testbenches' reference designs keep their clock. Both load enc_cur / enc_cur_n: enc_nxt on the falling
               clk_200MHz edge, i.e. the write clock after that edge, stable for a whole period. The pad delay is then
               the IOB's, independent of placement. At --phase-ps 0 C2 rises 2.5 ns after the write clock edge (the IOB
               path is about 1.25 ns faster than the source's fabric route, so the B pads land where 0x153C's and the
               release 0x153D's do, about 7.0 ns) and C3 --skew-ps (default 375) later: the release's A - B offset
               (0.36 ns). The fabric flops adc*_enc* stay, on the same edges, for the 0x33 self-check and the
               testbenches. The encode sequence is the source's (the same write clock value per edge).
--phase-ps N   the encode clock(s) moved by N ps (a multiple of 125, the VCO's 1/8 step): without --iob C1 from the
               source's 90 degrees (1.25 ns after clk_200MHz), with --iob both C2 and C3 from the point above.
--move S A     a placement roll: move the zaklad.v section whose header starts with S before the one starting with A
               (order only; the knob that re-rolls TD 4.6.8's placement, README section 17 "Rolls").

TD's PLL macro (arch/al3_macro.v) allows CLKCn_CPHASE 1..DIV (5 here); C0 = 0 degrees is CPHASE 4, so an output t ns
after C0 is CPHASE 4 + floor(e / 8), FPHASE e mod 8 with e = 8 t taken in -24..15 (mod 40). TD 4.6.8 accepts a CPHASE
above DIV without a message and times it as asked; this script never writes one. `encphase.txt` in the tree records the
options and the clock times; tb/run.sh reads the clocks from al_ip/pll.v.
"""
from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

SEP = '//-------------------------------------------------------------------------------------'

ENC_FLOP_OLD = """  always @(negedge clk_ADC)\t//clk_ADC   clk_RAM
    begin
      adc1_encB <= enc_nxt;
      adc2_encB <= enc_nxt;

      //The A and B clocks are inverted versions of each other
      adc1_encA <= enc_nxt_n;
      adc2_encA <= enc_nxt_n;
    end
"""
ENC_FLOP_NEW = """  //fpga-encphase (--iob): the encodes leave from IOB output registers (the ODDRs at the end of the file) on the
  //rising edges of clk_ADCB (B, PLL C2) and clk_ADCA (A, PLL C3). Their D is enc_cur / enc_cur_n, enc_nxt taken on
  //the falling clk_200MHz edge, i.e. the write clock as it is after that edge: stable from that edge to the next
  //falling edge, so any encode clock edge in between loads the same value (README section 19). adc*_enc* are fabric
  //copies on the same edges for the 0x33 self-check and the testbenches.
  reg enc_cur   = 0;
  reg enc_cur_n = 1;

  always @(negedge clk_200MHz)
    begin
      enc_cur   <= enc_nxt;
      enc_cur_n <= enc_nxt_n;
    end

  always @(posedge clk_ADCB)
    begin
      adc1_encB <= enc_cur;
      adc2_encB <= enc_cur;
    end

  always @(posedge clk_ADCA)
    begin
      //The A and B clocks are inverted versions of each other
      adc1_encA <= enc_cur_n;
      adc2_encA <= enc_cur_n;
    end
"""
PIN_OLD = """  assign o_adc1_encA = adc1_encA;
  assign o_adc1_encB = adc1_encB;
  assign o_adc2_encA = adc2_encA;
  assign o_adc2_encB = adc2_encB;
"""
PIN_NEW = """  //fpga-encphase (--iob): each encode pin from an IOB output register (AL_LOGIC_ODDR with both data inputs equal = a
  //single-rate register on the rising clock edge). TD 4.6.8 does not pack plain flops into these pads (PACKREG = ON,
  //synthesis keep and a posedge flop were all tried: #oreg stays 11), but it packs an ODDR, so the pad delay is the
  //IOB's own: the same on all four pins and in every placement.
  AL_LOGIC_ODDR enc_oddr_1A (.q(o_adc1_encA), .clk(clk_ADCA), .d1(enc_cur_n), .d2(enc_cur_n), .rst(1'b0));
  AL_LOGIC_ODDR enc_oddr_1B (.q(o_adc1_encB), .clk(clk_ADCB), .d1(enc_cur),   .d2(enc_cur),   .rst(1'b0));
  AL_LOGIC_ODDR enc_oddr_2A (.q(o_adc2_encA), .clk(clk_ADCA), .d1(enc_cur_n), .d2(enc_cur_n), .rst(1'b0));
  AL_LOGIC_ODDR enc_oddr_2B (.q(o_adc2_encB), .clk(clk_ADCB), .d1(enc_cur),   .d2(enc_cur),   .rst(1'b0));
"""
WIRE_OLD = "  wire clk_ADC;//\n"
WIRE_NEW = ("  wire clk_ADC;//\n"
            "  wire clk_ADCB;                           //fpga-encphase: the B encodes' clock (PLL C2)\n"
            "  wire clk_ADCA;                           //fpga-encphase: the A encodes' clock (PLL C3)\n")
INST_OLD = "    .clk1_out (clk_ADC)\n"
INST_NEW = "    .clk1_out (clk_ADC),\n    .clk2_out (clk_ADCB),\n    .clk3_out (clk_ADCA)\n"

PLL_C0_CPHASE = 4          # pll.v: C0 (clk_200MHz) at 0 degrees; TD's macro: 1 <= CPHASE <= DIV (5)
SRC_C1_NS = 1.25           # pll.v: C1 = 1 VCO cycle + 2/8 after C0 (VCO 1 GHz, 125 ps per FPHASE step)
IOB_B_NS = 5.0             # --iob, --phase-ps 0: C2 rises 2.5 ns after the falling clk_200MHz (write clock) edge


def replace_once(s: str, old: str, new: str, what: str) -> str:
    n = s.count(old)
    if n != 1:
        sys.exit(f'{what}: expected the text once, found it {n} times (has src/ changed?)')
    return s.replace(old, new)


def apply_iob(zaklad: str) -> str:
    zaklad = replace_once(zaklad, ENC_FLOP_OLD, ENC_FLOP_NEW, 'encode flops')
    zaklad = replace_once(zaklad, WIRE_OLD, WIRE_NEW, 'clk_ADC wire')
    zaklad = replace_once(zaklad, INST_OLD, INST_NEW, 'pll instance')
    return replace_once(zaklad, PIN_OLD, PIN_NEW, 'encode pins')


def pll_setting(t_ns: float) -> tuple[int, int]:
    """(CPHASE, FPHASE) of a PLL output rising t_ns after C0 (mod 5 ns), in the macro's legal range CPHASE 1..5."""
    e = round(t_ns * 8) % 40
    if e > 15:
        e -= 40                                    # -24 .. 15 eighths = CPHASE 1 .. 5
    return PLL_C0_CPHASE + e // 8, e % 8


def clock_times(phase_ps: int, iob: bool, skew_ps: int) -> tuple[float, float | None]:
    """Rising edge after C0, ns mod 5: C1 (without --iob), or C2 and C3 (the B and A encodes, with --iob)."""
    for v in (phase_ps, skew_ps):
        if v % 125:
            sys.exit("--phase-ps and --skew-ps must be multiples of 125 (the VCO's 1/8 step)")
    if not iob:
        return (SRC_C1_NS + phase_ps / 1000) % 5, None
    b = (IOB_B_NS + phase_ps / 1000) % 5
    return b, (b + skew_ps / 1000) % 5


def apply_pll(pll: str, t1: float, t2: float | None, label: str) -> str:
    """Without --iob: C1 (clk_ADC) to t1. With --iob: C1 unchanged (unused), C2 (clk_ADCB) to t1, C3 (clk_ADCA) to t2."""
    if t2 is None:
        c1, f1 = pll_setting(t1)
        pll = replace_once(pll, '.CLKC1_CPHASE(5),', f'.CLKC1_CPHASE({c1}),', 'pll.v CLKC1_CPHASE')
        pll = replace_once(pll, '.CLKC1_FPHASE("2")', f'.CLKC1_FPHASE("{f1}")', 'pll.v CLKC1_FPHASE')
        return replace_once(pll, '| 90.0000 DEG  ', f'| {t1 / 5 * 360:.4f} DEG ({label})', 'pll.v header')
    (c2, f2), (c3, f3) = pll_setting(t1), pll_setting(t2)
    pll = replace_once(pll, '.CLKC1_FPHASE("2")',
                       '.CLKC1_FPHASE("2"),\n'
                       f'\t\t.CLKC2_ENABLE("ENABLE"),\n\t\t.CLKC2_DIV(5),\n\t\t.CLKC2_CPHASE({c2}),\n'
                       f'\t\t.CLKC2_FPHASE("{f2}"),\n'
                       f'\t\t.CLKC3_ENABLE("ENABLE"),\n\t\t.CLKC3_DIV(5),\n\t\t.CLKC3_CPHASE({c3}),\n'
                       f'\t\t.CLKC3_FPHASE("{f3}")', 'pll.v C2 / C3')
    pll = replace_once(pll, '\t\tclk1_out);', '\t\tclk1_out,\n\t\tclk2_out,\n\t\tclk3_out);', 'pll.v ports')
    pll = replace_once(pll, '\toutput clk1_out;\n', '\toutput clk1_out;\n\toutput clk2_out;\n\toutput clk3_out;\n',
                       'pll.v outputs')
    pll = replace_once(pll, 'open, open, clk1_out, clk0_buf}', 'clk3_out, clk2_out, clk1_out, clk0_buf}', 'pll.v clkc')
    head = (f'| 90.0000 DEG  \n//\t\tC2        \t| 200.000000MHZ\t| {t1 / 5 * 360:.4f} DEG ({label}: the B encodes)'
            f'\n//\t\tC3        \t| 200.000000MHZ\t| {t2 / 5 * 360:.4f} DEG (the A encodes)')
    return replace_once(pll, '| 90.0000 DEG  ', head, 'pll.v header')


def move_section(text: str, sec: str, anc: str) -> str:
    lines = text.split('\n')
    starts = [i for i, ln in enumerate(lines) if ln.rstrip('\r') == SEP]

    def hits(ls, st, prefix):
        return [k for k, i in enumerate(st) if i + 1 < len(ls) and ls[i + 1].startswith(prefix)]

    h = hits(lines, starts, sec)
    if len(h) != 1:
        sys.exit(f'--move: section {sec!r} matches {len(h)}')
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
    ap.add_argument('--iob', action='store_true')
    ap.add_argument('--phase-ps', type=int, default=0)
    ap.add_argument('--skew-ps', type=int, default=375, help='--iob: A encodes this much after B + T/2 (default 375)')
    ap.add_argument('--move', nargs=2, action='append', default=[], metavar=('SECTION', 'ANCHOR'))
    a = ap.parse_args()
    if a.out.exists():
        sys.exit(f'{a.out} exists')
    shutil.copytree(a.src, a.out)
    zp, pllp = a.out / 'zaklad.v', a.out / 'al_ip' / 'pll.v'
    z, pll = rd(zp), rd(pllp)
    t1, t2 = clock_times(a.phase_ps, a.iob, a.skew_ps)
    if a.iob:
        z = apply_iob(z)
    if a.iob or a.phase_ps:
        pll = apply_pll(pll, t1, t2, f'fpga-encphase iob={int(a.iob)} {a.phase_ps:+d} ps')
    for sec, anc in a.move:
        z = move_section(z, sec, anc)
    wr(zp, z)
    wr(pllp, pll)
    if t2 is None:
        clocks = f'C1_NS={t1:.3f}'
    else:
        clocks = f'B_NS={t1:.3f} A_NS={t2:.3f}'
    moves = ';'.join(s + '>' + t for s, t in a.move) or '-'
    desc = (f'iob={int(a.iob)} phase_ps={a.phase_ps} skew_ps={a.skew_ps if a.iob else 0} {clocks} '
            f'moves={moves}\n')
    (a.out / 'encphase.txt').write_text(desc, encoding='ascii')
    print(desc, end='')


if __name__ == '__main__':
    main()
