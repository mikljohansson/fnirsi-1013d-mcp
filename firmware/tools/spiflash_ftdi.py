#!/usr/bin/env python3
"""Read, verify and write a 25-series SPI NOR flash with any FTDI MPSSE adapter (pyftdi).

Intended use in this project: the 1 MB FPGA config flash (Winbond 25Q80) on the FNIRSI 1013D,
through its 2x3 header. See docs/hardware-prep.md. Generic for any 3.3 V 25xx flash <= 16 MB.

Wiring (pyftdi SpiController, MPSSE channel A, mode 0):
    AD0 = SCK   -> flash CLK (pin 6)
    AD1 = MOSI  -> flash DI  (pin 5)
    AD2 = MISO  <- flash DO  (pin 2)
    AD3 = CS0   -> flash /CS (pin 1)
    GND         -> flash GND (pin 4)
Leave the adapter's 3.3 V output unconnected when the target board is powered by its own battery.

Install:  pip install pyftdi        (needs libusb; on Windows install a WinUSB/libusbK driver for the
                                     adapter's interface A with Zadig, and leave interface B alone)

Commands (all take --url, --freq):
    id                          print the JEDEC ID and the status register
    read OUT [--size N]         one read to OUT
    dump PREFIX [--size N]      read twice to PREFIX_A.bin and PREFIX_B.bin, compare, print sha256
    verify FILE                 compare chip with FILE (padded with 0xFF to chip size if shorter and --pad)
    write FILE --write --backup BACKUP [--pad]
                                erase+program+verify. Refuses to run unless --write is given, the chip
                                content equals BACKUP (a full dump), and you type the JEDEC ID to confirm.

UNTESTED ON HARDWARE. This script has only been run against a simulated flash (--selftest). For the
Sipeed FT2232D on Windows use firmware/tools/spiflash_d2xx.py instead (FTDI D2XX driver, no Zadig). This
pyftdi version is the alternative for Linux/WSL with usbipd, and for FT2232H/FT232H adapters.

FT2232D caveat: the FT2232D is not an H-series chip and rejects the MPSSE opcodes 0x8A, 0x97 and 0x8D.
pyftdi 0.57.2 handles 0x8A and 0x8D itself (it sends them only for H-series devices, ftdi.py
_set_frequency and enable_3phase_clock; SPI mode 0 never needs 3-phase). It does NOT guard 0x97: SpiController
.configure() always calls enable_adaptive_clock(False) (spi.py line 433). A D answers with a bad-command
reply (0xFA 0x97) that stays in the receive buffer. open_port() below purges the buffers after configure() to
discard it, and the JEDEC ID check will fail loudly if anything is still out of step.

Do not use `write` before the backups in docs/hardware-prep.md section 3 exist.
"""
import argparse
import hashlib
import sys
import time

SECTOR = 4096
PAGE = 256
CHUNK = 4096

CMD_WREN = 0x06
CMD_RDSR = 0x05
CMD_READ = 0x03
CMD_PP = 0x02
CMD_SE = 0x20
CMD_RDID = 0x9F

KNOWN = {  # JEDEC ID -> name. From research/notes/repo-atlan4.md (spi_control.c table) and datasheets.
    0xEF4014: "Winbond W25Q80",
    0xEF4015: "Winbond W25Q16",
    0xEF4016: "Winbond W25Q32",
    0x5E6014: "Zetta ZB25VQ80",
}


class Flash:
    """25xx flash on a pyftdi-style SPI port. `port.exchange(out, readlen)` returns bytes."""

    def __init__(self, port):
        self.p = port
        self.jedec = 0
        self.size = 0

    # basic commands
    def _x(self, out, readlen=0):
        return bytes(self.p.exchange(bytes(out), readlen))

    def read_id(self):
        r = self._x([CMD_RDID], 3)
        self.jedec = (r[0] << 16) | (r[1] << 8) | r[2]
        if self.jedec in (0x000000, 0xFFFFFF):
            raise SystemExit("No flash answered (JEDEC ID %06X). Check wiring, power and pinout." % self.jedec)
        cap = r[2]
        self.size = (1 << cap) if 0x10 <= cap <= 0x18 else 0
        return self.jedec

    def status(self):
        return self._x([CMD_RDSR], 1)[0]

    def wait_ready(self, timeout=5.0):
        t = time.time()
        while self.status() & 1:
            if time.time() - t > timeout:
                raise SystemExit("Timeout waiting for the flash to finish an operation")

    def wren(self):
        self._x([CMD_WREN])

    @staticmethod
    def _a(addr):
        return [(addr >> 16) & 0xFF, (addr >> 8) & 0xFF, addr & 0xFF]

    # data operations
    def read(self, addr, n):
        out = bytearray()
        while n:
            k = min(n, CHUNK)
            out += self._x([CMD_READ] + self._a(addr), k)
            addr += k
            n -= k
        return bytes(out)

    def erase_sector(self, addr):
        self.wren()
        self._x([CMD_SE] + self._a(addr))
        self.wait_ready(timeout=2.0)

    def program_page(self, addr, data):
        assert len(data) <= PAGE and (addr % PAGE) + len(data) <= PAGE
        self.wren()
        self._x([CMD_PP] + self._a(addr) + list(data))
        self.wait_ready(timeout=1.0)


def sha(b):
    return hashlib.sha256(b).hexdigest()


def need_size(fl, args):
    size = args.size or fl.size
    if not size:
        raise SystemExit("Cannot derive the chip size from JEDEC ID %06X; pass --size" % fl.jedec)
    return size


def describe(fl):
    name = KNOWN.get(fl.jedec, "unknown part")
    print("JEDEC ID %06X (%s), capacity %d bytes, status register 0x%02X" % (fl.jedec, name, fl.size, fl.status()))


def load_image(path, size, pad):
    data = open(path, "rb").read()
    if len(data) > size:
        raise SystemExit("%s is %d bytes, larger than the chip (%d)" % (path, len(data), size))
    if len(data) < size:
        if not pad:
            raise SystemExit("%s is %d bytes, chip is %d. Pass --pad to fill the rest with 0xFF" % (path, len(data), size))
        data += b"\xff" * (size - len(data))
    return data


def do_write(fl, args, size):
    if not args.write:
        raise SystemExit("Refusing to write without --write")
    if not args.backup:
        raise SystemExit("Refusing to write without --backup <full dump of the chip>")
    image = load_image(args.file, size, args.pad)
    backup = open(args.backup, "rb").read()
    if len(backup) != size:
        raise SystemExit("Backup is %d bytes, chip is %d" % (len(backup), size))
    sr = fl.status()
    if sr & 0x7C:  # BP0-BP2, TB, SEC: block protection bits (Winbond layout)
        raise SystemExit("Status register 0x%02X has block-protect bits set. Clear them deliberately first." % sr)
    print("Reading the chip to compare it with the backup...")
    cur = fl.read(0, size)
    if cur != backup:
        raise SystemExit("Chip content differs from --backup (chip %s, backup %s). Make a fresh dump first."
                         % (sha(cur)[:16], sha(backup)[:16]))
    print("Chip equals backup %s. New image sha256 %s" % (sha(cur)[:16], sha(image)))
    ans = input("Type the JEDEC ID %06X to start writing: " % fl.jedec).strip().upper()
    if ans != "%06X" % fl.jedec:
        raise SystemExit("Not confirmed, nothing written")
    erased = programmed = 0
    for s in range(0, size, SECTOR):
        old = cur[s:s + SECTOR]
        new = image[s:s + SECTOR]
        if old == new:
            continue
        need_erase = any((o & n) != n for o, n in zip(old, new))
        if need_erase:
            fl.erase_sector(s)
            erased += 1
            old = b"\xff" * SECTOR
        for q in range(0, SECTOR, PAGE):
            po, pn = old[q:q + PAGE], new[q:q + PAGE]
            if po != pn:
                fl.program_page(s + q, pn)
                programmed += 1
        if (s // SECTOR) % 32 == 0:
            print("  %6d / %d" % (s, size), end="\r", flush=True)
    print("\nErased %d sectors, programmed %d pages. Verifying..." % (erased, programmed))
    back = fl.read(0, size)
    if back != image:
        first = next(i for i in range(size) if back[i] != image[i])
        raise SystemExit("VERIFY FAILED, first difference at 0x%X. Do not power-cycle; restore the backup." % first)
    print("VERIFIED. sha256 %s" % sha(back))


def run(fl, args):
    fl.read_id()
    describe(fl)
    c = args.cmd
    if c == "id":
        return
    size = need_size(fl, args)
    if c == "read":
        d = fl.read(0, size)
        open(args.out, "wb").write(d)
        print("%s  %d bytes  sha256 %s" % (args.out, len(d), sha(d)))
    elif c == "dump":
        a = fl.read(0, size)
        b = fl.read(0, size)
        open(args.prefix + "_A.bin", "wb").write(a)
        open(args.prefix + "_B.bin", "wb").write(b)
        print("A sha256 %s\nB sha256 %s" % (sha(a), sha(b)))
        if a != b:
            raise SystemExit("DUMPS DIFFER. Do not trust either. Check wiring, lead length, --freq.")
        print("IDENTICAL, %d bytes. Written %s_A.bin and %s_B.bin" % (len(a), args.prefix, args.prefix))
    elif c == "verify":
        img = load_image(args.file, size, args.pad)
        cur = fl.read(0, size)
        if cur == img:
            print("MATCH sha256 %s" % sha(cur))
        else:
            n = sum(1 for x, y in zip(cur, img) if x != y)
            raise SystemExit("MISMATCH: %d differing bytes (chip sha256 %s)" % (n, sha(cur)))
    elif c == "write":
        do_write(fl, args, size)


def open_port(args):
    from pyftdi.spi import SpiController  # imported late so --selftest works without pyftdi
    ctrl = SpiController(cs_count=1)
    ctrl.configure(args.url)
    try:  # drop the 0xFA 0x97 reply a non-H FTDI (FT2232D) sends for pyftdi's unguarded adaptive-clock opcode
        ctrl._ftdi.purge_buffers()
    except Exception as exc:  # private attribute; do not fail if a pyftdi release changes it
        print("warning: could not purge FTDI buffers: %s" % exc, file=sys.stderr)
    return ctrl.get_port(cs=args.cs, freq=args.freq, mode=0)


class FakeFlash:
    """Behavioural model of a 1 MB 25Q80 for --selftest."""

    def __init__(self, size=1 << 20):
        self.mem = bytearray(b"\xff" * size)
        self.wel = False
        self.busy = 0

    def exchange(self, out, readlen=0):
        c = out[0]
        if c == CMD_RDID:
            return bytes([0xEF, 0x40, 0x14])[:readlen]
        if c == CMD_RDSR:
            return bytes([(1 if self.busy else 0)]) if readlen else b""
        if c == CMD_WREN:
            self.wel = True
            return b""
        addr = (out[1] << 16) | (out[2] << 8) | out[3] if len(out) >= 4 else 0
        if c == CMD_READ:
            return bytes(self.mem[addr:addr + readlen])
        if c == CMD_SE:
            assert self.wel
            a = addr & ~(SECTOR - 1)
            self.mem[a:a + SECTOR] = b"\xff" * SECTOR
            self.wel = False
        elif c == CMD_PP:
            assert self.wel
            data = out[4:]
            assert (addr % PAGE) + len(data) <= PAGE
            for i, v in enumerate(data):
                self.mem[addr + i] &= v
            self.wel = False
        return b""


def selftest():
    import os
    import tempfile
    fake = FakeFlash()
    fake.mem[:] = bytes((i * 7) & 0xFF for i in range(len(fake.mem)))
    fl = Flash(fake)
    d = tempfile.mkdtemp()
    os.chdir(d)
    ns = argparse.Namespace(cmd="dump", prefix="t", size=0, url="", file=None, pad=False, write=False, backup=None, out=None)
    run(fl, ns)
    new = bytes((i * 13 + 5) & 0xFF for i in range(282898))
    open("new.bin", "wb").write(new)
    ns = argparse.Namespace(cmd="write", prefix=None, size=0, file="new.bin", pad=True, write=True, backup="t_A.bin", out=None)
    sys.stdin = __import__("io").StringIO("EF4014\n")
    run(Flash(fake), ns)
    assert bytes(fake.mem) == new + b"\xff" * ((1 << 20) - len(new))
    print("selftest OK")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--url", default="ftdi://ftdi:2232/1", help="pyftdi URL; interface 1 = channel A. "
                    "List devices with: python3 -m pyftdi.ftdi (or ftdi_urls.py)")
    ap.add_argument("--freq", type=float, default=1e6, help="SPI clock in Hz (default 1 MHz)")
    ap.add_argument("--cs", type=int, default=0)
    ap.add_argument("--size", type=lambda x: int(x, 0), default=0, help="override chip size in bytes")
    ap.add_argument("--selftest", action="store_true", help="run against a simulated flash and exit")
    sub = ap.add_subparsers(dest="cmd")
    sub.add_parser("id")
    p = sub.add_parser("read"); p.add_argument("out")
    p = sub.add_parser("dump"); p.add_argument("prefix")
    p = sub.add_parser("verify"); p.add_argument("file"); p.add_argument("--pad", action="store_true")
    p = sub.add_parser("write"); p.add_argument("file"); p.add_argument("--pad", action="store_true")
    p.add_argument("--write", action="store_true"); p.add_argument("--backup")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not args.cmd:
        ap.error("a command is required")
    run(Flash(open_port(args)), args)


if __name__ == "__main__":
    main()
