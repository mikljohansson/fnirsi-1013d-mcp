#!/usr/bin/env python3
"""Compare a TD-built FPGA .bin with Atlan4's reference image.
Usage: python3 -I compare_bin.py <built.bin> <reference.bin>
Prints sizes, sha256, the header words, the count of differing bytes, and where the
byte pair 0x15 0x32 (version word of zaklad.v) occurs in each file."""
import hashlib, sys

def info(path):
    data = open(path, 'rb').read()
    lead = len(data) - len(data.lstrip(b'\xff'))
    hdr = data[lead:lead + 16].hex()
    hdr = ' '.join(hdr[i:i + 8] for i in range(0, len(hdr), 8))
    pos, hits = 0, []
    while True:
        pos = data.find(b'\x15\x32', pos)
        if pos < 0: break
        hits.append(pos); pos += 1
    print(f'{path}\n  size {len(data)}  sha256 {hashlib.sha256(data).hexdigest()}')
    print(f'  leading 0xFF bytes {lead}; first words after: {hdr}')
    print(f'  0x15 0x32 occurrences: {len(hits)}; first offsets {[hex(h) for h in hits[:8]]}')
    return data

a, b = info(sys.argv[1]), info(sys.argv[2])
n = min(len(a), len(b))
diff = sum(1 for i in range(n) if a[i] != b[i])
first = next((i for i in range(n) if a[i] != b[i]), None)
print(f'differing bytes in common length {n}: {diff} ({100.0 * diff / n:.2f} %); first at {hex(first) if first is not None else "-"}')
print('byte-identical' if a == b else 'not identical')
