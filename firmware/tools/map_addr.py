#!/usr/bin/env python3
"""Map scope firmware addresses to function + offset (and source line), and decode the restart record.

Usage (WSL Ubuntu, repo at /mnt/d/src/fnirsi-1013d-ai):

  python3 firmware/tools/map_addr.py ELF ADDR [ADDR ...]
  python3 firmware/tools/map_addr.py ELF --restart V49 V50 V51 V52 V53 V54 V55 V56
  python3 firmware/tools/map_addr.py ELF --disasm 6 ADDR

ADDR is hex (0x prefix optional). For each address the script prints the enclosing symbol + offset, the memory
region (image text/data/bss, a mode stack, the restart record, SRAM vectors, I/O) and, for code, the source line
(addr2line) and the instructions around it (objdump).

--restart takes GET_LINK_STATS counters 49-56 of fw 0.2.19+ (docs/protocol.md section 14 row 38): restarts, exception
type, exception LR, SPSR, exception SP, reset LR, reset SP, reset CPSR. Values may be decimal or 0x hex. It applies the
ARM LR offsets (data abort: fault PC = LR - 8; undefined / prefetch / SWI: LR - 4; an IRQ taken: interrupted PC =
LR - 4) and reports which mode and stack were active at _reset.

Tools: arm-none-eabi-nm / addr2line / objdump from $CROSS (prefix), else ~/opt/gcc10/usr/bin, /home/mikl/opt/gcc10,
then PATH. Only reads the ELF; run with python3 -I if the ELF came from somewhere untrusted.
"""

import argparse
import bisect
import os
import shutil
import subprocess
import sys

# Mode stacks from firmware/scope/start.s (full descending: each stack grows down from its _end to the next lower _end)
STACK_TOPS = [
    ("und", 0x81FB5000),
    ("abt", 0x81FC1800),
    ("irq", 0x81FDA800),
    ("fiq", 0x81FE7000),
    ("svc", 0x81FF3800),   # main program runs in SVC mode
    ("sys", 0x82000000),
]

FIXED_REGIONS = [
    (0x00000000, 0x00000040, "SRAM 0: low exception vectors + literals copied by start.s (a NULL write lands here)"),
    (0x00000040, 0x00008000, "SRAM A (boot ROM / bootloader scratch)"),
    (0x01C00000, 0x01D00000, "I/O registers"),
    (0x81A00000, 0x81A00040, "restart record (pc_hal_scope.c PC_RST_ADDR)"),
    (0x81BFFC00, 0x81C00000, "bootloader's copy of SD sector 710 (display/touch config)"),
    (0xFFFF0000, 0xFFFF8000, "boot ROM (high vectors, FEL at 0xFFFF0020)"),
]

MODES = {0x10: "usr", 0x11: "fiq", 0x12: "irq", 0x13: "svc", 0x17: "abt", 0x1B: "und", 0x1F: "sys"}
EXC_TYPES = {0: "jump to the image entry (no exception)", 1: "undefined instruction", 2: "SWI", 3: "prefetch abort",
             4: "data abort", 5: "reserved vector", 6: "FIQ"}
# fault/return PC = LR - offset
EXC_LR_OFFSET = {1: 4, 2: 4, 3: 4, 4: 8, 5: 4, 6: 4}


def find_tool(name):
    cross = os.environ.get("CROSS")
    cands = []
    if cross:
        cands.append(cross + name)
    for d in (os.path.expanduser("~/opt/gcc10/usr/bin"), "/home/mikl/opt/gcc10/usr/bin"):
        cands.append(os.path.join(d, "arm-none-eabi-" + name))
    for c in cands:
        if os.path.isfile(c) and os.access(c, os.X_OK):
            return c
    p = shutil.which("arm-none-eabi-" + name)
    if p:
        return p
    sys.exit("map_addr: arm-none-eabi-%s not found (set CROSS=/path/prefix-)" % name)


def run(args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def parse_int(s):
    s = s.strip()
    return int(s, 0) if s.lower().startswith("0x") or not all(c in "0123456789" for c in s) else int(s)


def parse_addr(s):
    return int(s, 16)


class Image:
    def __init__(self, elf):
        self.elf = elf
        self.nm = find_tool("nm")
        self.addr2line = find_tool("addr2line")
        self.objdump = find_tool("objdump")
        self.syms = []          # (addr, size, type, name), sorted
        self.sections = []      # (start, end, name, is_code)
        self.marks = {}
        for line in run([self.nm, "-n", "-S", "--defined-only", elf]).splitlines():
            parts = line.split()
            if len(parts) == 4:
                a, sz, t, name = parts
                a, sz = int(a, 16), int(sz, 16)
            elif len(parts) == 3:
                a, t, name = parts
                a, sz = int(a, 16), 0
            else:
                continue
            if name.startswith("$") or t in "aA":
                if t in "aA":
                    self.marks[name] = a
                continue
            if name in ("PRGM_START", "PRGM_END", "BSS_START", "BSS_END"):
                self.marks[name] = a
            self.syms.append((a, sz, t, name))
        self.syms.sort()
        self.addrs = [s[0] for s in self.syms]
        out = run([self.objdump, "-h", elf]).splitlines()
        for i, line in enumerate(out):
            p = line.split()
            if len(p) >= 7 and p[0].isdigit():
                name, size, vma = p[1], int(p[2], 16), int(p[3], 16)
                flags = out[i + 1] if i + 1 < len(out) else ""
                if "ALLOC" in flags and size:
                    self.sections.append((vma, vma + size, name, "CODE" in flags))

    def symbol(self, addr):
        i = bisect.bisect_right(self.addrs, addr) - 1
        # prefer the nearest symbol at or below addr whose size covers it; fall back to the nearest one below
        j = i
        while j >= 0 and j > i - 64:
            a, sz, t, name = self.syms[j]
            if sz and a <= addr < a + sz:
                return a, sz, t, name, True
            j -= 1
        if i >= 0:
            a, sz, t, name = self.syms[i]
            return a, sz, t, name, False
        return None

    def section(self, addr):
        for s, e, name, code in self.sections:
            if s <= addr < e:
                return name, code
        return None, False

    def region(self, addr):
        name, code = self.section(addr)
        if name:
            return "image section %s" % name, code
        bs, be = self.marks.get("BSS_START"), self.marks.get("BSS_END")
        if bs is not None and bs <= addr < be:
            return "bss", False
        for s, e, what in FIXED_REGIONS:
            if s <= addr < e:
                return what, False
        # each stack spans [next lower top, its top); the und stack's bottom is not defined in start.s (64 KiB assumed)
        lo = STACK_TOPS[0][1] - 0x10000
        for mode, top in STACK_TOPS:
            if lo <= addr <= top:
                return "%s stack (top 0x%08X, %d bytes below it)" % (mode, top, top - addr), False
            lo = top
        if 0x80000000 <= addr < 0x82000000:
            return "DDR outside image/bss/stacks", False
        return "unmapped", False

    def line(self, addr):
        out = run([self.addr2line, "-f", "-i", "-C", "-e", self.elf, "0x%x" % addr]).strip().splitlines()
        pairs = [(out[k], out[k + 1]) for k in range(0, len(out) - 1, 2)]
        return " <- ".join("%s at %s" % (f, os.path.basename(l)) for f, l in pairs)

    def disasm(self, addr, n):
        start, stop = max(addr - 4 * n, 0), addr + 4 * (n + 1)
        out = run([self.objdump, "-d", "--no-show-raw-insn", "--start-address=0x%x" % start,
                   "--stop-address=0x%x" % stop, self.elf])
        lines = []
        for l in out.splitlines():
            s = l.strip()
            if not s or ":" not in s or s.startswith("Disassembly") or "file format" in s:
                continue
            try:
                a = int(s.split(":")[0], 16)
            except ValueError:
                lines.append("        " + s)
                continue
            lines.append(("    --> " if a == addr else "        ") + s)
        return lines

    def describe(self, addr, label="", ndis=0):
        head = "0x%08X%s" % (addr, ("  (" + label + ")") if label else "")
        reg, code = self.region(addr)
        sym = self.symbol(addr)
        if sym and (code or reg in ("bss",) or reg.startswith("image")):
            a, sz, t, name, inside = sym
            where = "%s+0x%X" % (name, addr - a) + ("" if inside or not sz else "  (past its end, size 0x%X)" % sz)
        else:
            where = "-"
        print("%s  %s  [%s]" % (head, where, reg))
        if code:
            print("    source: " + self.line(addr))
            if ndis:
                for l in self.disasm(addr & ~3, ndis):
                    print(l)


def decode_restart(img, vals, ndis):
    if len(vals) != 8:
        sys.exit("--restart needs the 8 values of counters 49-56")
    restarts, etype, elr, spsr, esp, rlr, rsp, rcpsr = vals
    print("restarts since cold boot / ENTER_FEL: %d" % restarts)
    if restarts == 0:
        print("no restart recorded (cold boot, or the record was cleared by ENTER_FEL)")
        return
    mode = MODES.get(rcpsr & 0x1F, "?%02X" % (rcpsr & 0x1F))
    print("at _reset: CPSR 0x%08X = mode %s, I=%d F=%d T=%d" % (rcpsr, mode, (rcpsr >> 7) & 1, (rcpsr >> 6) & 1,
                                                                  (rcpsr >> 5) & 1))
    print("exception type %d: %s" % (etype, EXC_TYPES.get(etype, "?")))
    if etype:
        smode = MODES.get(spsr & 0x1F, "?%02X" % (spsr & 0x1F))
        off = EXC_LR_OFFSET.get(etype, 4)
        print("SPSR 0x%08X: the faulting code ran in mode %s (I=%d)" % (spsr, smode, (spsr >> 7) & 1))
        img.describe(elr - off, "fault PC = exception LR - %d" % off, ndis)
        img.describe(elr, "exception LR")
        img.describe(esp, "exception-mode SP")
        print("note: the reset LR/SP/CPSR below were taken after pc_exception_common, i.e. in the exception mode")
    else:
        if mode == "irq":
            print("_reset entered in IRQ mode: an IRQ vectored to the entry. Likely the low vector table at SRAM 0 was")
            print("overwritten (a NULL-pointer write), or the IRQ handler table pointed at 0. LR - 4 is the code the")
            print("IRQ interrupted, not the culprit.")
            img.describe(rlr - 4, "interrupted PC = LR_irq - 4", ndis)
        elif mode in ("svc", "sys", "usr"):
            print("_reset entered in %s mode: a jump to 0 / 0x80000000 from main-loop code. If the jump was a call" % mode)
            print("(blx/mov lr,pc) LR is just past the call site; after a corrupt return (pop {pc}) LR is stale.")
            img.describe(rlr - 4, "LR - 4 (call site if the jump was a call)", ndis)
        else:
            img.describe(rlr - 4, "LR - 4", ndis)
        img.describe(rlr, "reset LR")
    img.describe(rsp, "SP at _reset")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("elf")
    ap.add_argument("addrs", nargs="*", help="hex addresses (or, with --restart, the 8 counter values)")
    ap.add_argument("--restart", action="store_true", help="decode GET_LINK_STATS counters 49-56")
    ap.add_argument("--disasm", type=int, default=0, metavar="N", help="show N instructions either side of code addresses")
    a = ap.parse_intermixed_args()

    img = Image(a.elf)
    if a.restart:
        decode_restart(img, [parse_int(v) for v in a.addrs], a.disasm or 4)
    else:
        if not a.addrs:
            ap.error("no addresses")
        for s in a.addrs:
            img.describe(parse_addr(s), "", a.disasm)


if __name__ == "__main__":
    main()
