#!/usr/bin/env python3
"""SPI NOR flash tool for the FTDI FT2232D (Sipeed USB-JTAG/TTL RISC-V Debugger) via D2XX.

Backs up, verifies, erases and programs the 1 MB Winbond W25Q80DV FPGA configuration flash of the
FNIRSI 1013D. Windows-native: uses the `ftd2xx` package (FTDI CDM driver), no libusb/WinUSB/pyftdi,
so the adapter's driver stays untouched.   Install:  python -m pip install --user ftd2xx

Only MPSSE channel A of the FT2232D is used (D2XX serial "A" suffix).  SPI mode 0, MSB first.

Wiring (Sipeed board JTAG pins -> flash, 3.3 V logic ONLY):
    TCK (AD0) -> flash CLK   (SOIC-8 pin 6)
    TDI (AD1) -> flash DI    (pin 5, MOSI)
    TDO (AD2) <- flash DO    (pin 2, MISO)
    TMS (AD3) -> flash /CS   (pin 1)
    GND       -> flash GND   (pin 4)
    Do NOT connect the adapter's 3.3 V/5 V output to the board.

Commands: probe | read | verify | write | erase | wiring      (see `--help` of each)
"""
import argparse
import datetime
import hashlib
import os
import sys
import time

try:
    import ftd2xx
except ImportError:  # pragma: no cover
    ftd2xx = None

WIRING = """\
SPI wiring, Sipeed USB-JTAG/TTL debugger (FT2232D channel A, MPSSE) to the 25Q80 flash

  Debugger JTAG pin   FT2232D   SPI function     W25Q80 SOIC-8 pin
  -----------------   -------   --------------   -----------------
  TCK                 ADBUS0    SCK  (out)       6  CLK
  TDI                 ADBUS1    MOSI (out)       5  DI  (IO0)
  TDO                 ADBUS2    MISO (in)        2  DO  (IO1)
  TMS                 ADBUS3    /CS  (out, idle high)  1  /CS
  GND                 GND       ground           4  GND

  Flash pins 3 (/WP) and 7 (/HOLD) must be high (3.3 V) or left to the board's pull-ups.
  3.3 V logic only. Do not connect the adapter's power pins to the scope. Power the scope OFF for
  in-circuit reads (or see README-spiflash.md for the powered alternative).

NOTE: the TCK/TDI/TDO/TMS -> ADBUS0..3 mapping is the standard FTDI JTAG convention. A web search did
not find the Sipeed board's own schematic, so the silkscreen labels are UNCONFIRMED: `probe` prints the
idle pin state, and a flash that answers the JEDEC ID proves the mapping. If it reads 00/FF, check the
board labels with a meter against ADBUS0..3 before assuming a flash fault.
"""

# (mfr, type, capacity) -> (name, size in bytes)
CHIPS = {
    (0xEF, 0x40, 0x14): ("Winbond W25Q80DV", 1 << 20),
    (0xEF, 0x40, 0x15): ("Winbond W25Q16", 2 << 20),
    (0xEF, 0x40, 0x16): ("Winbond W25Q32", 4 << 20),
    (0xEF, 0x40, 0x17): ("Winbond W25Q64", 8 << 20),
    (0x5E, 0x60, 0x14): ("Zbit ZB25VQ80", 1 << 20),
    (0x5E, 0x60, 0x15): ("Zbit ZB25VQ16", 2 << 20),
    (0x5E, 0x60, 0x16): ("Zbit ZB25VQ32", 4 << 20),
    (0xC8, 0x40, 0x14): ("GigaDevice GD25Q80", 1 << 20),
    (0xC8, 0x40, 0x15): ("GigaDevice GD25Q16", 2 << 20),
    (0xC2, 0x20, 0x14): ("Macronix MX25L8006E", 1 << 20),
}

PAGE = 256
SECTOR = 4096
BLOCK = 65536
READ_CHUNK = 4096

CMD_WRSR, CMD_PP, CMD_READ, CMD_WRDI, CMD_RDSR, CMD_WREN = 0x01, 0x02, 0x03, 0x04, 0x05, 0x06
CMD_SE, CMD_UID, CMD_JEDEC, CMD_BE64, CMD_CE = 0x20, 0x4B, 0x9F, 0xD8, 0xC7

# MPSSE opcodes valid on the FT2232D
M_SETLOW, M_GETLOW, M_LOOPOFF, M_DIV, M_FLUSH = 0x80, 0x81, 0x85, 0x86, 0x87
M_OUT_NEG, M_IN_POS = 0x11, 0x20

CS_BIT = 0x08
LOW_DIR = 0x0B          # SCK, MOSI, CS outputs; MISO input
LOW_IDLE = CS_BIT       # SCK low, MOSI low, CS high
LOW_ACTIVE = 0x00

BASE_CLOCK = 12_000_000


class FlashError(Exception):
    pass


def progress(label, done, total, t0):
    if total <= 0:
        return
    el = max(time.time() - t0, 1e-6)
    sys.stderr.write("\r%-8s %6.1f%%  %7d/%d KiB  %6.1f KiB/s " %
                     (label, 100.0 * done / total, done >> 10, total >> 10, done / 1024 / el))
    if done >= total:
        sys.stderr.write("\n")
    sys.stderr.flush()


class Mpsse:
    """FT2232D channel A in MPSSE mode, SPI mode 0."""

    def __init__(self, serial=b"A", clock=1_000_000, timeout_ms=1000):
        if ftd2xx is None:
            raise FlashError("ftd2xx not installed: python -m pip install --user ftd2xx")
        self.dev = None
        self.timeout = timeout_ms / 1000.0
        try:
            self.dev = ftd2xx.openEx(serial)
        except Exception as e:
            raise FlashError("cannot open FTDI device %r (unplugged, or another program has it open?): %s"
                             % (serial, e))
        d = self.dev
        try:
            d.resetDevice()
            d.setTimeouts(timeout_ms, timeout_ms)
            d.setLatencyTimer(2)
            d.purge()
            d.setBitMode(0x00, 0x00)
            time.sleep(0.02)
            d.setBitMode(0x00, 0x02)
            time.sleep(0.05)
            d.purge()
            self._sync()
            div = int(round(BASE_CLOCK / (2.0 * clock))) - 1
            div = max(0, min(0xFFFF, div))
            self.actual_clock = BASE_CLOCK / ((1 + div) * 2)
            d.write(bytes([M_LOOPOFF, M_DIV, div & 0xFF, div >> 8, M_SETLOW, LOW_IDLE, LOW_DIR]))
        except Exception:
            self.close()
            raise

    def _sync(self):
        for op in (0xAA, 0xAB):
            self.dev.write(bytes([op]))
            r = self._read_exact(2, "MPSSE sync 0x%02X" % op)
            if r != bytes([0xFA, op]):
                raise FlashError("MPSSE sync failed for 0x%02X: got %s" % (op, r.hex()))

    def close(self):
        if self.dev is not None:
            try:
                self.dev.write(bytes([M_SETLOW, LOW_IDLE, 0x00]))  # release all pins
            except Exception:
                pass
            try:
                self.dev.setBitMode(0x00, 0x00)
            except Exception:
                pass
            try:
                self.dev.close()
            except Exception:
                pass
            self.dev = None

    def _read_exact(self, n, what="read"):
        buf = b""
        deadline = time.time() + self.timeout + n / 50000.0
        while len(buf) < n:
            q = self.dev.getQueueStatus()
            if q:
                buf += self.dev.read(min(q, n - len(buf)))
                continue
            if time.time() > deadline:
                try:
                    self.dev.purge()
                except Exception:
                    pass
                raise FlashError("%s: short read, got %d of %d bytes" % (what, len(buf), n))
            time.sleep(0.0005)
        return buf

    def pins(self):
        self.dev.write(bytes([M_GETLOW]))
        return self._read_exact(1, "pin read")[0]

    def xfer(self, tx, rxlen=0):
        """CS low; shift `tx` out (mode 0); then shift `rxlen` bytes in; CS high."""
        cmd = bytearray([M_SETLOW, LOW_ACTIVE, LOW_DIR])
        if tx:
            n = len(tx) - 1
            cmd += bytes([M_OUT_NEG, n & 0xFF, n >> 8]) + bytes(tx)
        if rxlen:
            n = rxlen - 1
            cmd += bytes([M_IN_POS, n & 0xFF, n >> 8])
        cmd += bytes([M_SETLOW, LOW_IDLE, LOW_DIR, M_FLUSH])
        if self.dev.write(bytes(cmd)) != len(cmd):
            raise FlashError("short USB write")
        return self._read_exact(rxlen, "SPI read") if rxlen else b""


class Flash:
    def __init__(self, m):
        self.m = m
        self.jedec = None
        self.name = None
        self.size = None

    def read_id(self):
        r = self.m.xfer(bytes([CMD_JEDEC]), 3)
        self.jedec = tuple(r)
        return self.jedec

    def identify(self, quiet=False):
        j = self.read_id()
        if j in ((0, 0, 0), (0xFF, 0xFF, 0xFF)):
            raise FlashError("no chip detected (ID ff ff ff or 00 00 00): got %s" % self.jedec_str())
        if j in CHIPS:
            self.name, self.size = CHIPS[j]
        else:
            cap = j[2]
            if not 0x10 <= cap <= 0x1A:
                raise FlashError("unknown chip %s and implausible capacity byte" % self.jedec_str())
            self.name, self.size = "unknown (JEDEC %s)" % self.jedec_str(), 1 << cap
            if not quiet:
                print("WARNING: chip %s not in table; assuming %d bytes from capacity byte"
                      % (self.jedec_str(), self.size), file=sys.stderr)
        return self.name, self.size

    def jedec_str(self):
        return " ".join("%02x" % b for b in self.jedec)

    def status(self):
        return self.m.xfer(bytes([CMD_RDSR]), 1)[0]

    def unique_id(self):
        return self.m.xfer(bytes([CMD_UID, 0, 0, 0, 0]), 8)

    def wait_ready(self, timeout=60.0, what="operation"):
        t0 = time.time()
        while True:
            if not self.status() & 1:
                return
            if time.time() - t0 > timeout:
                raise FlashError("timeout waiting for %s (WIP stuck)" % what)
            time.sleep(0.001)

    def write_enable(self):
        self.m.xfer(bytes([CMD_WREN]))
        if not self.status() & 2:
            raise FlashError("write enable did not set WEL (status 0x%02x); chip not responding or protected"
                             % self.status())

    @staticmethod
    def _addr(a):
        return bytes([(a >> 16) & 0xFF, (a >> 8) & 0xFF, a & 0xFF])

    def read(self, addr, n, label="read"):
        out = bytearray()
        t0 = time.time()
        while len(out) < n:
            c = min(READ_CHUNK, n - len(out))
            out += self.m.xfer(bytes([CMD_READ]) + self._addr(addr + len(out)), c)
            progress(label, len(out), n, t0)
        return bytes(out)

    def erase_sector(self, addr):
        self.write_enable()
        self.m.xfer(bytes([CMD_SE]) + self._addr(addr))
        self.wait_ready(5.0, "sector erase at 0x%x" % addr)

    def erase_chip(self):
        self.write_enable()
        self.m.xfer(bytes([CMD_CE]))
        self.wait_ready(120.0, "chip erase")

    def program_page(self, addr, data):
        assert 0 < len(data) <= PAGE and (addr % PAGE) + len(data) <= PAGE
        self.write_enable()
        self.m.xfer(bytes([CMD_PP]) + self._addr(addr) + bytes(data))
        self.wait_ready(2.0, "page program at 0x%x" % addr)

    def unprotect(self):
        self.write_enable()
        self.m.xfer(bytes([CMD_WRSR, 0x00]))
        self.wait_ready(2.0, "status write")


# ---------------------------------------------------------------- helpers

def sha(b):
    return hashlib.sha256(b).hexdigest()


def open_flash(args, need_chip=True):
    m = Mpsse(args.serial.encode(), args.clock)
    f = Flash(m)
    try:
        f.identify()
    except Exception:
        m.close()
        raise
    return m, f


def repo_root():
    return os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def make_backup(f, tag):
    d = os.path.join(repo_root(), "backups")
    os.makedirs(d, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    path = os.path.join(d, "%s-%s.bin" % (stamp, tag))
    print("Backing up whole chip (%d bytes), two reads for comparison ..." % f.size)
    a = f.read(0, f.size, "backup1")
    b = f.read(0, f.size, "backup2")
    if a != b:
        raise FlashError("backup reads differ (sha256 %s vs %s); bus unstable, refusing to continue" % (sha(a), sha(b)))
    with open(path, "xb") as fh:
        fh.write(a)
    with open(path + ".sha256", "w") as fh:
        fh.write("%s  %s\n" % (sha(a), os.path.basename(path)))
    print("Backup: %s\n  sha256 %s" % (path, sha(a)))
    return a


def confirm(f, args, what):
    if not args.yes:
        raise FlashError("%s requires --yes" % what)
    want = "".join("%02x" % b for b in f.jedec)
    print("About to %s %s (%s, %d bytes)." % (what, f.name, f.jedec_str(), f.size))
    if getattr(args, "confirm_id", None):
        got = args.confirm_id.strip().lower().replace(" ", "")
    elif not sys.stdin.isatty():
        raise FlashError("%s requires an interactive terminal for the typed confirmation (or --confirm-id)" % what)
    else:
        got = input("Type the JEDEC ID (%s) to confirm: " % want).strip().lower().replace(" ", "")
    if got != want:
        raise FlashError("confirmation mismatch, aborted")


def check_protect(f, args):
    sr = f.status()
    if sr & 0x7C:  # BP0-2, TB, SEC
        if args.unprotect:
            print("Clearing block-protect bits (status was 0x%02x)" % sr)
            f.unprotect()
        else:
            raise FlashError("status register 0x%02x shows block protection set; use --unprotect to clear" % sr)


# ---------------------------------------------------------------- commands

def cmd_wiring(args):
    print(WIRING)
    return 0


def cmd_probe(args):
    m = Mpsse(args.serial.encode(), args.clock)
    try:
        print("MPSSE sync OK on channel %s, SPI clock %.3f MHz" % (args.serial, m.actual_clock / 1e6))
        p = m.pins()
        print("ADBUS idle state 0x%02x: SCK=%d MOSI=%d MISO=%d CS=%d"
              % (p, p & 1, p >> 1 & 1, p >> 2 & 1, p >> 3 & 1))
        f = Flash(m)
        try:
            name, size = f.identify()
        except FlashError as e:
            print("JEDEC ID: %s" % f.jedec_str())
            print(str(e).split(":")[0] if "no chip" in str(e) else e)
            return 1
        print("JEDEC ID: %s" % f.jedec_str())
        print("Chip: %s, %d bytes (%d KiB)" % (name, size, size >> 10))
        print("Status register: 0x%02x (WIP=%d WEL=%d BP=0x%x)" % (f.status(), f.status() & 1,
                                                                  f.status() >> 1 & 1, f.status() >> 2 & 7))
        if args.uid:
            print("Unique ID: %s" % f.unique_id().hex())
        return 0
    finally:
        m.close()


def cmd_read(args):
    if os.path.exists(args.output) and not args.force:
        print("refusing to overwrite %s (use --force)" % args.output, file=sys.stderr)
        return 2
    m, f = open_flash(args)
    try:
        size = args.size or f.size
        if size > f.size:
            raise FlashError("--size larger than chip")
        a = f.read(0, size, "read")
        if args.twice:
            b = f.read(0, size, "read2")
            if a != b:
                first = next(i for i in range(size) if a[i] != b[i])
                raise FlashError("the two reads DIFFER (first at 0x%x); bus unstable, nothing written" % first)
            print("two reads identical")
        with open(args.output, "wb") as fh:
            fh.write(a)
        print("wrote %s (%d bytes)\nsha256 %s" % (args.output, size, sha(a)))
        return 0
    finally:
        m.close()


def compare(a, b):
    first, cnt = None, 0
    for i in range(min(len(a), len(b))):
        if a[i] != b[i]:
            cnt += 1
            if first is None:
                first = i
    return first, cnt


def cmd_verify(args):
    with open(args.input, "rb") as fh:
        want = fh.read()
    m, f = open_flash(args)
    try:
        if len(want) > f.size:
            raise FlashError("file (%d) larger than chip (%d)" % (len(want), f.size))
        got = f.read(0, len(want), "verify")
        first, cnt = compare(got, want)
        if cnt:
            print("MISMATCH: %d differing bytes, first at offset 0x%x (chip %02x, file %02x)"
                  % (cnt, first, got[first], want[first]))
            return 1
        print("OK: chip matches %s over %d bytes (sha256 %s)" % (args.input, len(want), sha(want)))
        if len(want) < f.size:
            print("note: file is shorter than the chip (%d of %d bytes compared)" % (len(want), f.size))
        return 0
    finally:
        m.close()


def cmd_erase(args):
    if args.chip == bool(args.range):
        print("give exactly one of --chip or --range START:LENGTH", file=sys.stderr)
        return 2
    m, f = open_flash(args)
    try:
        confirm(f, args, "ERASE")
        if not args.no_backup:
            make_backup(f, "pre-erase")
        check_protect(f, args)
        if args.chip:
            f.erase_chip()
            start, length = 0, f.size
        else:
            s, l = (int(x, 0) for x in args.range.split(":"))
            if s % SECTOR or l % SECTOR or s + l > f.size or l <= 0:
                raise FlashError("range must be 4 KiB aligned and inside the chip")
            t0 = time.time()
            for i in range(0, l, SECTOR):
                f.erase_sector(s + i)
                progress("erase", i + SECTOR, l, t0)
            start, length = s, l
        blank = f.read(start, length, "blank")
        if blank.count(0xFF) != length:
            print("ERASE FAILED: %d non-FF bytes remain" % (length - blank.count(0xFF)))
            return 1
        print("erase OK, range blank")
        return 0
    finally:
        m.close()


def cmd_write(args):
    with open(args.input, "rb") as fh:
        data = fh.read()
    pad = int(args.pad, 0) & 0xFF
    if args.chip_erase and args.no_erase:
        print("--chip-erase and --no-erase conflict", file=sys.stderr)
        return 2
    m, f = open_flash(args)
    try:
        if len(data) > f.size:
            print("WARNING: file is %d bytes, chip is %d: TRUNCATING the file" % (len(data), f.size))
            data = data[:f.size]
        elif len(data) < f.size:
            print("WARNING: file is %d bytes, chip is %d: PADDING with 0x%02x to the full chip size"
                  % (len(data), f.size, pad))
            data = data + bytes([pad]) * (f.size - len(data))
        confirm(f, args, "WRITE")
        cur = None
        if not args.no_backup:
            cur = make_backup(f, "pre-write")
        else:
            print("WARNING: --no-backup given; flash contents will NOT be saved")
        check_protect(f, args)
        # sectors that need erasing: any bit going 0 -> 1 (or everything if current content unknown)
        n_sec = f.size // SECTOR
        if cur is None:
            erase = [s for s in range(n_sec) if data[s * SECTOR:(s + 1) * SECTOR] != cur_blank(SECTOR)] \
                if False else list(range(n_sec))
        else:
            erase = []
            for s in range(n_sec):
                c = int.from_bytes(cur[s * SECTOR:(s + 1) * SECTOR], "big")
                t = int.from_bytes(data[s * SECTOR:(s + 1) * SECTOR], "big")
                if (~c & t) != 0:
                    erase.append(s)
        if args.no_erase:
            if cur is not None and erase:
                raise FlashError("--no-erase but %d sectors need erasing (first at 0x%x)" % (len(erase), erase[0] * SECTOR))
            erase = []
        if args.chip_erase:
            print("chip erase ...")
            f.erase_chip()
            cur = b"\xff" * f.size
        elif erase:
            print("erasing %d of %d sectors ..." % (len(erase), n_sec))
            t0 = time.time()
            for i, s in enumerate(erase):
                f.erase_sector(s * SECTOR)
                progress("erase", i + 1, len(erase), t0)
            cur = bytearray(cur) if cur is not None else bytearray(b"\xff" * f.size)
            for s in erase:
                cur[s * SECTOR:(s + 1) * SECTOR] = b"\xff" * SECTOR
        elif cur is None:
            cur = b"\xff" * f.size  # --no-erase without backup: assume blank
        pages = [p for p in range(f.size // PAGE) if data[p * PAGE:(p + 1) * PAGE] != cur[p * PAGE:(p + 1) * PAGE]]
        print("programming %d of %d pages ..." % (len(pages), f.size // PAGE))
        t0 = time.time()
        for i, p in enumerate(pages):
            f.program_page(p * PAGE, data[p * PAGE:(p + 1) * PAGE])
            if i % 16 == 0 or i + 1 == len(pages):
                progress("program", i + 1, len(pages), t0)
        got = f.read(0, f.size, "verify")
        first, cnt = compare(got, data)
        if cnt:
            print("VERIFY FAILED: %d differing bytes, first at 0x%x (chip %02x, want %02x)"
                  % (cnt, first, got[first], data[first]))
            return 1
        print("write verified OK, sha256 %s" % sha(data))
        return 0
    finally:
        m.close()


def cur_blank(n):  # kept trivial; unused branch above
    return b"\xff" * n


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                                 epilog=WIRING)
    ap.add_argument("--serial", default="A", help="D2XX serial of channel A (default 'A'; see listDevices)")
    ap.add_argument("--clock", type=float, default=1_000_000,
                    help="SPI clock in Hz, 12 MHz/((1+div)*2); default 1000000, max 6000000")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("probe", help="sync MPSSE, read pins and JEDEC ID")
    p.add_argument("--uid", action="store_true", help="also read the 64-bit unique ID")
    p = sub.add_parser("read", help="dump the chip to a file")
    p.add_argument("-o", "--output", required=True)
    p.add_argument("--size", type=lambda x: int(x, 0), default=0)
    p.add_argument("--twice", action="store_true", help="read twice and compare")
    p.add_argument("--force", action="store_true", help="overwrite existing output")
    p = sub.add_parser("verify", help="compare chip with a file")
    p.add_argument("-i", "--input", required=True)
    p = sub.add_parser("write", help="erase, program and verify (needs --yes and typed confirmation)")
    p.add_argument("-i", "--input", required=True)
    p.add_argument("--yes", action="store_true")
    p.add_argument("--confirm-id", help="non-interactive confirmation: must equal the chip's JEDEC ID, e.g. ef4014")
    p.add_argument("--no-backup", action="store_true")
    p.add_argument("--no-erase", action="store_true", help="program only; fails if sectors need erasing")
    p.add_argument("--sector-erase", action="store_true", help="erase only needed 4K sectors (default)")
    p.add_argument("--chip-erase", action="store_true")
    p.add_argument("--pad", default="0xFF", help="pad byte for short files (default 0xFF)")
    p.add_argument("--unprotect", action="store_true", help="clear block-protect bits first")
    p = sub.add_parser("erase", help="erase chip or a sector-aligned range (needs --yes and typed confirmation)")
    p.add_argument("--chip", action="store_true")
    p.add_argument("--range", help="START:LENGTH, 4 KiB aligned")
    p.add_argument("--yes", action="store_true")
    p.add_argument("--confirm-id", help="non-interactive confirmation: must equal the chip's JEDEC ID, e.g. ef4014")
    p.add_argument("--no-backup", action="store_true")
    p.add_argument("--unprotect", action="store_true")
    sub.add_parser("wiring", help="print wiring")
    args = ap.parse_args(argv)
    if args.clock > 6_000_000 or args.clock <= 0:
        ap.error("--clock must be in (0, 6000000]")
    fn = {"probe": cmd_probe, "read": cmd_read, "verify": cmd_verify, "write": cmd_write,
          "erase": cmd_erase, "wiring": cmd_wiring}[args.cmd]
    try:
        return fn(args)
    except FlashError as e:
        print("ERROR: %s" % e, file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("interrupted", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
