#!/usr/bin/env python3
"""Bench check of the scope's USB protocol link (needs pyserial and the scope on a COM port / ttyACM).

    python bench_link.py COM10                # latency, pass times, TEST_PATTERN sizes
    python bench_link.py /dev/ttyACM0 --fel   # ... then ENTER_FEL at the end
    python bench_link.py COM10 --census 2000  # acceptance census: random PINGs + 100 x 64 KiB, no retries;
                                              # every E_BAD_CRC is diffed against the request sent (GET_BAD_FRAME)

Prints one line per check; exit status 0 when every check passed. Uses only the frame format of
docs/protocol.md (CRCs from binascii / zlib), no project imports.
"""

import argparse
import binascii
import statistics
import struct
import sys
import time
import zlib

import serial  # pyserial

HDR = struct.Struct("<2sBBHHHI")


def frame(cmd, seq, payload=b""):
    h = HDR.pack(b"\xa5\x13", 1, 0, cmd, seq, 0, len(payload))
    h += struct.pack("<H", binascii.crc_hqx(h, 0xFFFF))
    if payload:
        h += payload + struct.pack("<I", zlib.crc32(payload) & 0xFFFFFFFF)
    return h


def read_exact(ser, n, deadline):
    buf = b""
    while len(buf) < n and time.monotonic() < deadline:
        buf += ser.read(n - len(buf))
    return buf


class Link:
    def __init__(self, port):
        self.ser = serial.Serial(port, 115200, timeout=0.05, write_timeout=2)
        self.seq = 100

    def request(self, cmd, payload=b"", timeout=3.0):
        self.seq = (self.seq + 1) & 0xFFFF
        t0 = time.perf_counter()
        self.ser.write(frame(cmd, self.seq, payload))
        deadline = time.monotonic() + timeout
        h = read_exact(self.ser, 16, deadline)
        if len(h) < 16:
            return None, None, time.perf_counter() - t0
        _, major, flags, rcmd, rseq, status, n = HDR.unpack(h[:14])
        if binascii.crc_hqx(h[:14], 0xFFFF) != struct.unpack("<H", h[14:16])[0] or rseq != self.seq:
            return None, None, time.perf_counter() - t0
        body = read_exact(self.ser, n + (4 if n else 0), deadline)
        dt = time.perf_counter() - t0
        if n and (len(body) < n + 4 or zlib.crc32(body[:n]) & 0xFFFFFFFF != struct.unpack("<I", body[n:n + 4])[0]):
            return status, None, dt
        return status, body[:n], dt

    def text(self, line, timeout=1.0):
        self.ser.write(line.encode() + b"\n")
        deadline = time.monotonic() + timeout
        out = b""
        while not out.endswith(b"\n") and time.monotonic() < deadline:
            out += self.ser.read(1)
        return out.decode(errors="replace").strip()


def census(L, n_ping, n_big):
    import random
    rnd = random.Random(1013)
    bad, lost, diffs = 0, 0, []
    for i in range(n_ping):
        p = bytes(rnd.randrange(256) for _ in range(rnd.randint(1, 200)))
        seq = (L.seq + 1) & 0xFFFF
        sent = frame(0x0001, seq, p)
        st, body, dt = L.request(0x0001, p, timeout=0.5)
        if st is None:
            lost += 1
            time.sleep(0.15)
            L.ser.reset_input_buffer()
        elif st == 1:
            bad += 1
            st2, bf, _ = L.request(0x0008)
            if st2 == 0 and bf:
                cnt, rxb, ln = struct.unpack_from('<IIH', bf)
                got = bf[12:12 + ln]
                d = [(k, sent[k], got[k]) for k in range(min(len(sent), len(got))) if sent[k] != got[k]]
                diffs.append((i, len(sent), rxb, d))
                print('     bad #%d: req %d B, rx_bytes %d, %d bytes differ: %s' % (
                    i, len(sent), rxb, len(d), ' '.join('@%d %02x->%02x' % x for x in d[:12])))
    big_lost = 0
    for i in range(n_big):
        st, body, dt = L.request(0x0006, struct.pack('<IB3x', 65536, 0), timeout=2.0)
        if st != 0 or body != pattern0(65536):
            big_lost += 1
            time.sleep(0.15)
            L.ser.reset_input_buffer()
    return bad, lost, big_lost, diffs


def pattern0(n):
    return bytes(i & 0xFF for i in range(n))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    ap.add_argument("--n", type=int, default=200, help="PING count")
    ap.add_argument("--fel", action="store_true", help="send ENTER_FEL at the end")
    ap.add_argument("--census", type=int, default=0, help="PING count for the no-retry census (also 100 x 64 KiB)")
    a = ap.parse_args()

    L = Link(a.port)
    fails = 0

    def check(ok, msg):
        nonlocal fails
        fails += 0 if ok else 1
        print(("OK   " if ok else "FAIL ") + msg)

    L.ser.reset_input_buffer()
    check(L.text("#RESET") == "#OK", "#RESET")
    ver = L.text("#VER")
    check(ver.startswith("#OK,FNIRSI-1013D-AI"), "#VER " + ver)

    st, info, _ = L.request(0x0002)
    check(st == 0 and info and len(info) >= 76, "GET_INFO caps=0x%04x" % (struct.unpack_from("<I", info, 20)[0] if info else 0))

    lat = []
    for i in range(a.n):
        st, body, dt = L.request(0x0001, bytes([i & 0xFF]))
        if st == 0 and body == bytes([i & 0xFF]):
            lat.append(dt * 1000)
    check(len(lat) == a.n, "PING x%d: median %.2f ms, p95 %.2f ms, max %.2f ms" % (
        len(lat), statistics.median(lat) if lat else 0, sorted(lat)[int(0.95 * len(lat)) - 1] if lat else 0, max(lat) if lat else 0))

    st, s, _ = L.request(0x0003)
    if s and len(s) >= 32:
        print("     GET_STATUS control=%d ui_busy=%d pass_ms=%d pass_max_ms=%d" % (s[0], s[3], *struct.unpack_from("<HH", s, 28)))

    for n in (492, 495, 496, 1008, 4076, 16384, 32748, 65536):
        st, body, dt = L.request(0x0006, struct.pack("<IB3x", n, 0), timeout=5.0)
        check(st == 0 and body == pattern0(n), "TEST_PATTERN %6d B in %7.1f ms" % (n, dt * 1000))

    t0 = time.perf_counter()
    reps, total = 20, 0
    for _ in range(reps):
        st, body, _ = L.request(0x0006, struct.pack("<IB3x", 65536, 2), timeout=5.0)
        total += len(body or b"")
    dt = time.perf_counter() - t0
    check(total == reps * 65536, "throughput %.2f MB/s (%d x 64 KiB)" % (total / dt / 1e6, reps))

    if a.census:
        bad, lost, big_lost, _ = census(L, a.census, 100)
        check(bad == 0 and lost == 0 and big_lost == 0,
              'census: %d PINGs: %d E_BAD_CRC, %d no response; 100 x 64 KiB: %d failed' % (a.census, bad, lost, big_lost))

    names = ('rx_packets rx_bytes rx_stalls rx_swept tx_packets tx_zlps tx_kicked tx_flushes link_resets dtr_up '
             'dtr_down frames hdr_crc payload_crc oversize version resp_dropped rx_timeouts junk_bytes responses '
             'duplicates tx_aborts disp_main disp_busy disp_isr held isr_skipped '
             # fw 0.2.0+: startup (27, 28), 0.2.2+: first conversion / capture (ms + 1 since 0.2.4)
             'init_to_first_poll_ms first_pass_ms first_conversion first_capture '
             # fw 0.2.4+: first-pass stage marks (ms + 1)
             'after_battery after_acquisition after_touch after_rtc '
             # fw 0.2.5+: USB device events
             'usb_bus_resets usb_suspends usb_resumes usb_disconnects ep0_setups ep0_bad_setups '
             # fw 0.2.7+: RX back-pressure branches
             'rx_isr_stalls rx_releases rx_restalls rx_empty_releases rx_count_max rx_count_oversize '
             # fw 0.2.12+: USB interrupts served by the timer poll (lost IRQs)
             'usb_irq_polled '
             # fw 0.2.17+: ms spent on the boot splash step + 1
             'boot_splash_ms').split()
    st, body, _ = L.request(0x0007)
    if st == 0 and body:
        n = struct.unpack_from('<H', body)[0]
        vals = struct.unpack_from('<%dI' % n, body, 4)
        print('     GET_LINK_STATS ' + ' '.join('%s=%d' % (names[i] if i < len(names) else 'v%d' % i, v) for i, v in enumerate(vals)))
    else:
        print('     GET_LINK_STATS not supported (fw < 0.1.4)')

    if a.fel:
        st, _, _ = L.request(0x0F01, struct.pack("<I", 0x4C45465F))
        check(st == 0, "ENTER_FEL answered OK; now expect USB 1f3a:efe8 within ~1 s")

    print("%d failures" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
