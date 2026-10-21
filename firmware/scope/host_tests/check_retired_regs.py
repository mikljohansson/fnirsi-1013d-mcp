#!/usr/bin/env python3
"""Fail if the scope firmware still addresses an FPGA command the supported bitstream no longer decodes.

0x153C (fpga-c4, fpga/al3/README.md section 15) dropped write 0x0F (sample_read_enable, a no-op mux on every Atlan4
bitstream) and the touch panel address write 0x3C / read 0x41. The firmware supports only the latest bitstream
(fw-one), so a literal fpga_write_cmd of one of them is dead traffic (fw-cruft). The model (fpga_model.c) does not
decode them either: this source check is what keeps them out.

0x153C also dropped reads 0x24..0x27 (the live ADC pins, 78ccaa0). The roll mode read 0x24 / 0x26 through a command
byte held in the channel settings (`averagecommand`, written later with fpga_write_cmd(settings->...)) until fw-roll
moved it onto the 0x153D logger stream, so the check also fails on a `...command = 0x..` assignment of a retired code.

0x153D's pattern generator was cut to the fractional square (owner, 2026-10-21; fw-nopat, fpga-d3): the repeat count
0x6E, the gap 0x6F, the pattern trigger engine's writes 0x76..0x7D and its status read 0x42 are gone with it.

Usage: python3 -I check_retired_regs.py <firmware/scope directory>
"""

import pathlib
import re
import sys

RETIRED = {
    0x0F: "sample_read_enable",
    0x24: "live ADC pins, CH1",
    0x25: "live ADC pins, CH1",
    0x26: "live ADC pins, CH2",
    0x27: "live ADC pins, CH2",
    0x3C: "touch panel address write",
    0x41: "touch panel address read",
    0x42: "pattern trigger engine status (cut)",
    0x6E: "pattern repeat count (cut)",
    0x6F: "pattern gap (cut)",
    **{c: "pattern trigger engine (cut)" for c in range(0x76, 0x7E)},
}
PATTERN = re.compile(r"\b(?:fpga_write_cmd|fpga_write_u24|seq_w8)\s*\(\s*0x([0-9A-Fa-f]{1,2})\s*\)")
#: a command byte kept in the channel settings (adc1command, the former averagecommand, ...) and written later
FIELD = re.compile(r"\b\w*command\s*=\s*0x([0-9A-Fa-f]{1,2})\b")


def main() -> int:
    root = pathlib.Path(sys.argv[1])
    hits = []
    for path in sorted(root.glob("*.[ch]")):
        for lineno, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            code = line.split("//", 1)[0]
            for m in list(PATTERN.finditer(code)) + list(FIELD.finditer(code)):
                cmd = int(m.group(1), 16)
                if cmd in RETIRED:
                    hits.append(f"{path.name}:{lineno}: 0x{cmd:02X} ({RETIRED[cmd]}): {line.strip()}")
    if hits:
        print("retired FPGA commands still addressed:")
        print("\n".join(hits))
        return 1
    print(f"check_retired_regs: OK (no access to {', '.join(f'0x{c:02X}' for c in RETIRED)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
