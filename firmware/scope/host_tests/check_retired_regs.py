#!/usr/bin/env python3
"""Fail if the scope firmware still addresses an FPGA command the supported bitstream no longer decodes.

0x153C (fpga-c4, fpga/al3/README.md section 15) dropped write 0x0F (sample_read_enable, a no-op mux on every Atlan4
bitstream) and the touch panel address write 0x3C / read 0x41. The firmware supports only the latest bitstream
(fw-one), so a literal fpga_write_cmd of one of them is dead traffic (fw-cruft). The model (fpga_model.c) does not
decode them either: this source check is what keeps them out.

Usage: python3 -I check_retired_regs.py <firmware/scope directory>
"""

import pathlib
import re
import sys

RETIRED = {0x0F: "sample_read_enable", 0x3C: "touch panel address write", 0x41: "touch panel address read"}
PATTERN = re.compile(r"\b(?:fpga_write_cmd|fpga_write_u24|seq_w8)\s*\(\s*0x([0-9A-Fa-f]{1,2})\s*\)")


def main() -> int:
    root = pathlib.Path(sys.argv[1])
    hits = []
    for path in sorted(root.glob("*.[ch]")):
        for lineno, line in enumerate(path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            code = line.split("//", 1)[0]
            for m in PATTERN.finditer(code):
                cmd = int(m.group(1), 16)
                if cmd in RETIRED:
                    hits.append(f"{path.name}:{lineno}: 0x{cmd:02X} ({RETIRED[cmd]}): {line.strip()}")
    if hits:
        print("retired FPGA commands still written:")
        print("\n".join(hits))
        return 1
    print(f"check_retired_regs: OK (no write of {', '.join(f'0x{c:02X}' for c in RETIRED)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
