#!/usr/bin/env python3
"""Generate docs/protocol-vectors.json: protocol v1.0 frame test vectors (docs/protocol.md section 4).

Frames are built here with the Python standard library CRCs only (binascii.crc_hqx for the header,
zlib.crc32 for the payload), independently of the firmware C code. The same file is consumed by
firmware/scope/host_tests/check_vectors.py (C parser cross-check) and by the MCP server tests.

Deterministic: running it twice gives the same file. Usage:
    python3 gen_vectors.py [-o ../../../docs/protocol-vectors.json]
"""

import argparse
import binascii
import json
import os
import struct
import zlib

MAGIC = b"\xa5\x13"
FLAG_RESPONSE = 0x01
MAX_REQUEST_PAYLOAD = 492

CMDS = {
    "PING": 0x0001, "GET_INFO": 0x0002, "GET_STATUS": 0x0003, "SET_REMOTE": 0x0004,
    "GET_BATTERY": 0x0005, "TEST_PATTERN": 0x0006, "GET_LINK_STATS": 0x0007, "GET_BAD_FRAME": 0x0008,
    "GET_EVENT_LOG": 0x0009, "GET_HEALTH": 0x000A,
    "GET_CHANNEL": 0x0100, "SET_CHANNEL": 0x0101, "GET_TIMEBASE": 0x0102, "SET_TIMEBASE": 0x0103,
    "GET_TRIGGER": 0x0104, "SET_TRIGGER": 0x0105,
    "ARM": 0x0200, "FORCE": 0x0201, "STOP": 0x0202, "WAIT": 0x0203, "READ_RAW": 0x0204,
    "READ_TRACE": 0x0205, "READ_LIVE": 0x0206, "ACQUIRE_AVG": 0x0207,
    "SET_ACQ_STATE": 0x0208, "GET_CAPTURE_INFO": 0x0209, "GET_DECIM": 0x020C, "SET_DECIM": 0x020D,
    "SET_SEQ": 0x020E, "GET_SEQ": 0x020F, "READ_SEQ": 0x0210, "BODE_CTRL": 0x0211, "READ_BODE": 0x0212,
    "SD_INFO": 0x0500, "SD_READ_SECTORS": 0x0501, "SD_STAGE": 0x0502, "SD_WRITE_SECTORS": 0x0503,
    "SAVE_REPORT": 0x0504,
    "GET_GEN": 0x0300, "SET_GEN": 0x0301, "GET_CAL": 0x0400, "READ_SCREEN": 0x0401, "CAL_PROBE": 0x0402,
    "DEBUG_FPGA": 0x0F00, "ENTER_FEL": 0x0F01,
}


def header(cmd, seq, status=0, flags=0, length=0, major=1):
    h = MAGIC + struct.pack("<BBHHHI", major, flags, cmd, seq, status, length)
    return h + struct.pack("<H", binascii.crc_hqx(h, 0xFFFF))


def frame(cmd, seq, payload=b"", status=0, flags=0, major=1):
    f = header(cmd, seq, status, flags, len(payload), major)
    if payload:
        f += payload + struct.pack("<I", zlib.crc32(payload) & 0xFFFFFFFF)
    return f


def decoded(cmd, seq, payload=b"", status=0, flags=0):
    return {"cmd": cmd, "seq": seq, "status": status, "flags": flags, "len": len(payload),
            "payload": payload.hex()}


def ev_frame(cmd, seq, payload=b"", status=0, flags=0):
    d = decoded(cmd, seq, payload, status, flags)
    d["event"] = "frame"
    return d


# Request payloads, one per command (field layouts: docs/protocol.md section 8)
REQUESTS = [
    ("PING", b"hi", "echo 2 bytes (spec section 13 example, seq 2)"),
    ("GET_INFO", b"", "spec section 13 example, seq 1"),
    ("GET_STATUS", b"", ""),
    ("SET_REMOTE", struct.pack("<BBH", 1, 0x00, 15), "acquire, flags 0, watchdog 15 s (1.3: accepted as a no-op)"),
    ("GET_BATTERY", b"", ""),
    ("TEST_PATTERN", struct.pack("<IB3x", 4076, 0), "4076 byte counter pattern: response frame is exactly 4096 bytes (ZLP case, test T1)"),
    ("GET_CHANNEL", struct.pack("<B", 1), "CH2"),
    ("SET_CHANNEL", struct.pack("<BBBBBBBBi", 0, 0x3F, 1, 5, 0, 0, 1, 0, -20),
     "CH1 all fields: enable, 100 mV/div (range 5), DC, no invert, 1x probe, UI dcoffset -20"),
    ("GET_TIMEBASE", b"", ""),
    ("SET_TIMEBASE", struct.pack("<B3xI", 0, 19), "UI index 19 (spec section 13 example, seq 3)"),
    ("GET_TRIGGER", b"", ""),
    ("SET_TRIGGER", struct.pack("<HBBBBHIIII", 0x00BF, 1, 1, 140, 1, 0, 2000, 16000, 0, 0),
     "CH2, falling, level 140, normal, pretrigger 2000, total 16000, auto_timeout default"),
    ("ARM", struct.pack("<I", 0), ""),
    ("FORCE", b"", ""),
    ("STOP", b"", ""),
    ("WAIT", struct.pack("<I", 1000), "timeout 1000 ms"),
    ("READ_RAW", struct.pack("<BBBBiII", 0x0F, 1, 0, 0, 0, 8192, 0), "all four cores, relative to capture start, per-core blocks, 8192 words"),
    ("READ_TRACE", struct.pack("<BBHII", 0x03, 0x00, 0, 0, 0), "both channels, everything"),
    ("READ_LIVE", struct.pack("<BBH", 0, 0, 100), "CH1 A, 100 bytes"),
    ("ACQUIRE_AVG", struct.pack("<HBBIIII", 64, 0x01, 0x01, 100, 0, 2000, 0), "64 captures, CH1, require trigger"),
    ("SET_ACQ_STATE", struct.pack("<B3x", 1), "1.3: UI RUN (0 = STOP)"),
    ("GET_CAPTURE_INFO", b"", "1.3: settings of the latest finished capture"),
    ("GET_DECIM", b"", "1.9: decimation filter setting and state (FPGA 0x1534+)"),
    ("SET_DECIM", struct.pack("<BBH", 2, 0, 0), "1.9: filter CIC2, no core-order swap"),
    ("SET_SEQ", struct.pack("<BBBBHHII", 1, 0, 7, 0, 32, 0, 1000, 0),
     "1.17: 32 segments in slots of 2^7 words, hold-off 1000 ns (FPGA 0x1536)"),
    ("GET_SEQ", b"", "1.17: sequencer setting, fit for the next ARM, FPGA 0x2A status, latest run"),
    ("READ_SEQ", struct.pack("<BBBBHHI", 0, 0, 0, 0, 0, 32, 0), "1.17: segment table entries 0..31"),
    ("GET_LINK_STATS", b"", "1.2: link counters"),
    ("GET_BAD_FRAME", b"", "1.2: last request with a payload CRC error"),
    ("GET_EVENT_LOG", b"", "1.5: the last 64 USB / link events with ms timestamps"),
    ("SD_INFO", struct.pack("<B3x", 1), "1.3: card, partition, write window, eGON headers, VERIFY checksums"),
    ("SD_READ_SECTORS", struct.pack("<IHH", 750, 128, 0), "1.3: sectors 750..877"),
    ("SD_STAGE", struct.pack("<IHH", 512, 4, 0) + b"eGON", "1.3: 4 bytes at stage offset 512"),
    ("SD_WRITE_SECTORS", struct.pack("<IHBBII", 750, 128, 0, 0, 0x1C291CA3, 0x52574453),
     "1.3: stage[0, 64 KiB) to sector 750, stage crc32 0x1C291CA3 (example value), magic 'SDWR'"),
    ("GET_GEN", b"", ""),
    ("SET_GEN", struct.pack("<BBBBQII", 0x07, 1, 0, 0, 10_000_000, 5000, 0), "enable, 10 kHz (10,000,000 mHz), 50.00 %"),
    ("GET_CAL", struct.pack("<B", 0), "CH1"),
    ("READ_SCREEN", struct.pack("<HH", 0, 40), "rows 0..39"),
    ("DEBUG_FPGA", bytes([1, 1, 0x06, 3, 2]), "write command 0x06, read 2 bytes (FPGA version)"),
    ("ENTER_FEL", struct.pack("<I", 0x4C45465F), "magic 'FEL_' little-endian"),
    # appended (fw 0.2.66): new requests go at the end so the earlier vectors keep their sequence numbers
    ("BODE_CTRL", struct.pack("<BBBBIIHH", 1, 1, 1, 0, 10_000, 100_000_000, 50, 0),
     "1.20: run one Bode sweep, settings apply: Normal settle, CH2 auto, 100 Hz (10,000 cHz) .. 1 MHz, 50 points"),
    ("READ_BODE", struct.pack("<HHI", 0, 0, 0), "1.20: the Bode header and every row that fits"),
    # appended (fw 0.2.67)
    ("CAL_PROBE", bytes([1, 0, 0, 0, 0]),
     "1.21 (provisional): the wizard's zero measurement of CH2, hardware range 0 (5 V/div), wizard readout, one range"),
]


# Decimation filter maths (FPGA 0x1534, docs/protocol.md section 14 row 47; FIR 0x1535, row 49), independent of
# pc_decim.c
DECIM_MAX_TOTAL = 8190
DECIM_LEVELS = [0, 1, 64, 127, 128, 200, 254, 255]
SAMPLE_RATE_DIVIDERS = [0, 1, 3, 9, 19, 39, 99, 199, 399, 999, 1999, 3999, 9999, 19999, 39999, 99999, 199999, 399999]


def decim_case(divider, k_req, total, fir_req=0):
    n = 2 * (divider // 2 + 1)
    k = k_req
    if k_req == 0 or divider == 0 or total > DECIM_MAX_TOTAL:
        k = 0
    # FIR (0x1535): engages for K >= 1, N >= 16, N % 4 == 0; the CIC then decimates by M = N / 2
    fir = 1 if (fir_req and k and n >= 16 and n % 4 == 0) else 0
    m = n // 2 if fir else n
    while k > 1 and 255 * m ** k >= 1 << 48:
        k -= 1
    case = {"divider": divider, "k_req": k_req, "fir_req": fir_req, "total": total, "k": k, "fir": fir}
    if k == 0:
        case.update({"n": 0, "gain": 1, "shift": 0, "rate": 200000000 // (divider + 1), "settle_us": 0, "ext_shift": 0,
                     "level16": []})
        return case
    gain = m ** k
    shift = max(0, (255 * gain).bit_length() - 16) + fir
    lv = []
    for v in DECIM_LEVELS:
        y = v * gain
        if shift:
            y = (y + (1 << (shift - 1))) >> shift
        lv.append(min(y, 65535))
    if fir:
        # (K + 10) N x 5 ns + 1 us; lag 250 ns + 17.5 N ns + 1.25 K (N - 2) ns, in 0.25 ns units
        settle = -(-((k + 10) * n) // 200) + 1
        ext = (1000 + 70 * n + 5 * k * (n - 2) + 10 * n) // (20 * n)
    else:
        settle = -(-((k + 2) * n) // 200)
        ext = (240 + k * (n - 1) * 5 + 5 * n) // (10 * n)
    case.update({"n": n, "gain": gain, "shift": shift, "rate": 200000000 // n, "settle_us": settle, "ext_shift": ext,
                 "level16": lv})
    return case


def decim_cases():
    cases = [decim_case(d, k, 3000) for d in SAMPLE_RATE_DIVIDERS for k in range(4)]
    cases += [decim_case(2, 2, 3000), decim_case(20, 3, 3000), decim_case(10332, 3, 3000), decim_case(10334, 3, 3000),
              decim_case(1050629, 2, 3000), decim_case(1050630, 2, 3000), decim_case(19, 2, 8190), decim_case(19, 2, 8192)]
    # 1.11 FIR: every table rate and K, then the eligibility edges (D 11/13 no, 14/15 yes) and the CIC3 limit at N/2
    cases += [decim_case(d, k, 3000, 1) for d in SAMPLE_RATE_DIVIDERS for k in range(4)]
    cases += [decim_case(d, 2, 3000, 1) for d in (11, 12, 13, 14, 15, 16, 17, 18)]
    cases += [decim_case(20667, 3, 3000, 1), decim_case(20671, 3, 3000, 1), decim_case(19, 2, 8192, 1)]
    return cases


# Sequencer of FPGA 0x1536 (protocol 1.17, docs/protocol.md section 14 row 56), independent of pc_seq.c
def seq_fit(mode, flags, m, count, n, pre, total, wide):
    if mode > 2:
        return 1
    if (flags & ~1) or ((flags & 1) and mode != 1):
        return 2
    if mode == 0:
        return 0
    if mode == 1:
        if m < 5 or m > 12:
            return 3
        if count == 0 or count > (8192 >> m):
            return 4
        if total + 2 > (1 << m):
            return 5
    else:
        if n == 0 or n > 65536 or (not wide and n > 257):
            return 7
        if total > 2046:
            return 5
        if count == 0 or count * total > 2048:
            return 4
    if total == 0 or pre >= total:
        return 6
    return 0


def seq_fit_case(mode, flags, m, count, n, pre, total, wide):
    if mode == 1:
        maxc = (8192 >> m) if 5 <= m <= 12 else 0
        slots = (8192 >> m) if (flags & 1) else count
    elif mode == 2:
        maxc = 0 if (total == 0 or total > 2046) else 2048 // total
        slots = 0
    else:
        maxc = slots = 0
    c57 = (mode & 3) | (4 if (mode == 1 and flags & 1) else 0)
    return {"op": "fit", "args": [mode, flags, m, count, n, pre, total, wide],
            "out": [seq_fit(mode, flags, m, count, n, pre, total, wide), maxc, slots, c57, count & 0xFFF, n & 0xFFFF]}


def seq_entry_case(w):
    return {"op": "entry", "args": ["%x" % w],
            "out": [w & 0xFFFFFFFFFF, (w >> 40) & 0x1FFF, (w >> 53) & 1, w >> 63]}


def seq_plan(total, pre, d):
    word = 10 * (d + 1)
    if total == 0 or total > 2046 or pre >= total or word > 1000000:
        return [0, 0]
    rec = total * word + total * 5 + 40 + (2 + pre) * word + 100
    n = 64
    while n >= 2 and n * rec > 40000 * 1000:
        n >>= 1
    if n < 2:
        return [0, 0]
    return [n, -(-(n * rec) // 1000)]


def seq_avg(s, c, gain, off):
    m8 = (s * 256 + c // 2) // c
    v = (m8 * gain + (off << 8) + (1 << 23)) >> 24
    return [min(255, max(0, v))]


def seq_cases():
    cases = []
    fits = [(1, 0, 7, 32, 0, 16, 120, 0), (1, 0, 7, 32, 0, 16, 126, 0), (1, 0, 7, 32, 0, 16, 127, 0),
            (1, 0, 7, 32, 0, 120, 120, 0), (1, 1, 7, 4, 0, 16, 120, 0), (1, 2, 7, 4, 0, 16, 120, 0),
            (1, 0, 12, 2, 0, 100, 4000, 1), (1, 0, 12, 3, 0, 100, 4000, 1), (1, 0, 13, 1, 0, 1, 20, 0),
            (1, 0, 0, 0, 0, 1, 20, 0), (2, 0, 0, 1, 257, 750, 1500, 0), (2, 0, 0, 1, 258, 750, 1500, 0),
            (2, 0, 0, 1, 65536, 750, 1500, 1), (2, 0, 0, 1, 0, 750, 1500, 1), (2, 0, 0, 2, 16, 50, 1024, 1),
            (2, 0, 0, 2, 16, 50, 1025, 1), (2, 0, 0, 1, 16, 50, 2047, 1), (2, 1, 0, 1, 16, 50, 1000, 1),
            (3, 0, 0, 1, 1, 1, 10, 0), (0, 0, 0, 0, 0, 9999, 0, 0)]
    fits += [(1, 0, m, 8192 >> m, 0, 4, (1 << m) - 2, w) for m in range(5, 13) for w in (0, 1)]
    cases += [seq_fit_case(*f) for f in fits]
    cases += [{"op": "hold", "args": [ns], "out": [(ns + 4) // 5]} for ns in (0, 1, 4, 5, 6, 999, 1000, 1001, 0xFFFFFFFF)]
    cases += [seq_entry_case(w) for w in (0x803FFFFFFFFFFFFF, 0x8020050000004E20, 0x0040000000000000,
                                           (1 << 63) | (93 << 40) | 123456789)]
    cases += [{"op": "segw", "args": [m, s, off, pre, j], "out": [(s << m) + ((off - pre + j) & ((1 << m) - 1))]}
              for (m, s, off, pre, j) in ((7, 3, 10, 16, 0), (7, 3, 10, 16, 5), (7, 3, 10, 16, 6), (7, 31, 89, 16, 110),
                                          (12, 1, 4095, 0, 1), (5, 255, 0, 31, 30))]
    cases += [{"op": "plan", "args": [t, p, d], "out": seq_plan(t, p, d)}
              for (t, p, d) in ((1500, 750, 0), (1500, 750, 1), (1500, 750, 199), (1500, 750, 1999), (2046, 750, 0),
                                (2047, 750, 0), (1500, 1500, 0), (1500, 375, 39), (1500, 1250, 99))]
    cases += [{"op": "avg", "args": [s, c, g, o], "out": seq_avg(s, c, g, o)}
              for (s, c, g, o) in ((201, 2, 65536, 0), (401, 4, 131072, 0), (765, 3, 70000, 1 << 20), (0, 3, 65536, -(1 << 20)),
                                   (65280, 256, 65536, 0), (12345, 97, 67000, -150000), (100 * 64 + 31, 64, 64000, 300000),
                                   (128, 1, 65536, 32767), (128, 1, 65536, 32768))]
    return cases


# Calibration data v3 (protocol row 51, adc_cal.h, firmware/README.md section 6.3): the sector 708 block at byte 128 and
# GET_CAL bytes 112..255. Example content = host_tests/test_adc_cal.c example_channels(): unit_ch1() on both channels plus
# edits on CH2. (The v2 section was dropped in fw 0.2.47: nothing reads it any more.)
ADC_CAL_PWM = [904, 900, 908, 903, 908, 903]


def adc_cal_v3_channel(edits=False):
    zero = [[[2107] * 6, [2067] * 6], [[2094] * 6, [2009] * 6]]           # [class full, gated][core A, B][range]
    slope = [[[-17257] * 6, [-16572] * 6], [[-17308] * 6, [-17026] * 6]]  # [class][core][range], Q16 codes per count
    noise = [[[12] * 6, [19] * 6], [[10] * 6, [21] * 6]]                   # [class][core][range], 1/16 code
    gain = [[32607, 33678], [32699, 32837]]                                # [class][core], Q15
    meta = {"gain_range": 2, "gain_flags": 0, "gain_lever": 1416}
    if edits:
        zero[0][1][4] = -77
        slope[0][0][5] = -20000
        noise[0][1][2] = 47
        gain[1][1] = 31000
        meta = {"gain_range": 0xFF, "gain_flags": 1, "gain_lever": 0}
    return dict({"pwm": list(ADC_CAL_PWM), "zero": zero, "slope": slope, "noise": noise, "gain": gain}, **meta)


def adc_cal_v3_pack(chs):
    b = struct.pack("<IHHHH", 0x4C414341, 3, 304, 2, 0)
    for c in chs:
        b += struct.pack("<6H", *c["pwm"])
        b += struct.pack("<24h", *[v for cls in c["zero"] for core in cls for v in core])
        b += struct.pack("<24h", *[v for cls in c["slope"] for core in cls for v in core])
        b += struct.pack("<24B", *[v for cls in c["noise"] for core in cls for v in core])
        b += struct.pack("<4H", *[v for cls in c["gain"] for v in cls])
        b += struct.pack("<BBH", c["gain_range"], c["gain_flags"], c["gain_lever"])
    return b + struct.pack("<I", zlib.crc32(b))


def adc_cal_v3_section():
    chs = [adc_cal_v3_channel(), adc_cal_v3_channel(True)]
    blk = adc_cal_v3_pack(chs)
    assert len(blk) == 304
    return {
        "notes": "Calibration data v3 block (protocol 1.12, row 51), sector 708 bytes 128..431, little-endian: u32 magic "
                 "'ACAL', u16 version 3, u16 length 304, u16 clock classes 2, u16 0; per channel (CH1, CH2, 144 bytes): "
                 "u16 pwm[6] (the dc_calibration_offset values it was measured at; ignored unless equal to sector "
                 "709's), i16 zero[class][core][range] (class 0 full rate = divider 0 and filtered, 1 gated = legacy "
                 "divider >= 1; core 0 A = FPGA 0x20/0x22, 1 B = 0x21/0x23; 1/16 code), i16 slope[class][core][range] "
                 "(Q16 codes per PWM count), u8 noise[class][core][range] (RMS, 1/16 code), u16 gain[class][core] (Q15, "
                 "32768 = 1), u8 gain_range (0xFF = never measured), u8 gain_flags (bit 0 carried over), u16 gain_lever "
                 "(1/16 code); u32 zlib.crc32 of the 300 bytes before it. GET_CAL (1.12) bytes 112..255 carry the same "
                 "per-channel layout for the requested channel.",
        "block_hex": blk.hex(),
        "channels": chs,
    }


# fw 0.2.52 (protocol 1.16, row 55): the three new stored layouts, packed here with struct and checked against the C
# packers (test_diag --layouts) by check_vectors.py. Same inputs as host_tests/test_diag.c layout_*().
def diag_layouts_section():
    life = {"flags": 1, "power_on": 12, "minutes": 2225,
            "restart": {"type": 4, "restarts": 2, "power_on": 11, "exc_lr": 0x80012348, "spsr": 0x6000001F,
                        "exc_sp": 0x81F00000, "reset_lr": 0x80000020, "reset_sp": 0x81F80000,
                        "reset_cpsr": 0x600000D3, "fsr": 0x8, "far": 0xDEADBEEF, "minutes": 2100}}
    r = life["restart"]
    w = struct.pack("<HHIIHHHHI", 0x11FE, life["flags"], life["power_on"], life["minutes"], 1, r["restarts"], r["type"], 0,
                    r["power_on"])
    w += struct.pack("<10I", r["exc_lr"], r["spsr"], r["exc_sp"], r["reset_lr"], r["reset_sp"], r["reset_cpsr"], r["fsr"],
                     r["far"], r["minutes"], 0)
    assert len(w) == 64
    prov = {"build": 0x8614032A, "version": "0.2.52", "fpga": 0x1535, "what": 0x0F, "rtc": 1, "uptime_ms": 420000,
            "power_on": 12, "minutes": 2225, "time": [26, 10, 19, 12, 34, 56]}
    p = struct.pack("<IHHI16sHBBIII6BH", 0x56525041, 1, 56, prov["build"], prov["version"].encode(), prov["fpga"],
                    prov["what"], prov["rtc"], prov["uptime_ms"], prov["power_on"], prov["minutes"], *prov["time"], 0)
    p += struct.pack("<I", zlib.crc32(p))
    assert len(p) == 56
    ident = {"version": "0.2.52", "build": 0x1234ABCD, "time": [26, 10, 19, 12, 34, 56], "proto": [1, 16]}
    i = struct.pack("<II16sI6BBB", 0x44495746, 0x33313031, ident["version"].encode(), ident["build"], *ident["time"],
                    *ident["proto"])
    assert len(i) == 36
    return {
        "notes": "Protocol 1.16 (row 55) stored layouts, little-endian. life: settings sector 709 words 224..255 as "
                 "bytes (u16 marker 0x11FE, u16 flags (bit 0 session open), u32 power_on, u32 operating minutes, u16 "
                 "restart flags (bit 0 stored), u16 restarts, u16 exception type, u16 0, u32 power_on of the record, "
                 "u32 exception LR, SPSR, exception SP, reset LR, reset SP, reset CPSR, fault status, fault address, "
                 "operating minutes at the record, u32 0). prov: sector 708 bytes 432..487 (u32 'APRV', u16 version 1, "
                 "u16 length 56, u32 build, char[16] version, u16 FPGA word, u8 what (bit 0 CH1 zeros, 1 CH1 gains, 2 "
                 "CH2 zeros, 3 CH2 gains), u8 rtc, u32 uptime_ms, u32 power_on, u32 minutes, u8[6] time, u16 0, u32 "
                 "zlib.crc32 of the 52 bytes before). ident: the image identity record linked last in fw 0.2.52+ "
                 "(u32 'FWID', u32 '1013', char[16] version, u32 build, u8[6] commit time, u8 proto_major, u8 "
                 "proto_minor).",
        "life": dict(life, hex=w.hex()),
        "prov": dict(prov, hex=p.hex()),
        "ident": dict(ident, hex=i.hex()),
    }


def build():
    vectors = []

    def add(name, data, device_events, description="", frames=None):
        vectors.append({
            "name": name,
            "description": description,
            "hex": data.hex(),
            # Valid frames a direction-agnostic parser finds (host side: responses are valid too)
            "frames": frames if frames is not None else [],
            # Events the scope firmware's parser produces, in order (see top-level "device_events")
            "device_events": device_events,
        })

    # 1. one request per command
    seq = 10
    for name, payload, desc in REQUESTS:
        cmd = CMDS[name]
        s = {"PING": 2, "GET_INFO": 1, "SET_TIMEBASE": 3}.get(name, seq)
        f = frame(cmd, s, payload)
        add("req_" + name.lower(), f, [ev_frame(cmd, s, payload)], desc, [decoded(cmd, s, payload)])
        seq += 1

    # 2. spec section 13 responses
    f = frame(CMDS["PING"], 2, b"hi", flags=FLAG_RESPONSE)
    add("resp_ping", f, [], "PING response (spec section 13). The device parser drops frames with RESPONSE set",
        [decoded(CMDS["PING"], 2, b"hi", flags=FLAG_RESPONSE)])
    f = frame(CMDS["SET_CHANNEL"], 4, b"", status=6, flags=FLAG_RESPONSE)
    add("resp_set_channel_not_remote", f, [], "SET_CHANNEL response E_NOT_REMOTE (spec section 13)",
        [decoded(CMDS["SET_CHANNEL"], 4, b"", status=6, flags=FLAG_RESPONSE)])
    err_payload = struct.pack("<H", 1) + b"range 7 > 6"
    f = frame(CMDS["SET_CHANNEL"], 5, err_payload, status=4, flags=FLAG_RESPONSE)
    add("resp_error_with_text", f, [], "E_BAD_ARG with detail 1 and text (section 4.2)",
        [decoded(CMDS["SET_CHANNEL"], 5, err_payload, status=4, flags=FLAG_RESPONSE)])

    # 3. sizes
    p = bytes(i & 0xFF for i in range(MAX_REQUEST_PAYLOAD))
    f = frame(CMDS["PING"], 100, p)
    assert len(f) == 512
    add("req_ping_max_payload", f, [ev_frame(CMDS["PING"], 100, p)], "492-byte payload: the largest request frame, 512 bytes",
        [decoded(CMDS["PING"], 100, p)])
    p1 = bytes((i * 7) & 0xFF for i in range(MAX_REQUEST_PAYLOAD + 1))
    f = frame(CMDS["PING"], 101, p1)
    add("req_oversize_493", f, [{"event": "bad_len", "cmd": CMDS["PING"], "seq": 101}],
        "len 493 > 492 with a valid header CRC: device answers E_BAD_LEN at the header and discards the 493 + 4 following bytes",
        [decoded(CMDS["PING"], 101, p1)])
    f2 = frame(CMDS["GET_STATUS"], 103)
    add("req_oversize_then_frame", f + f2,
        [{"event": "bad_len", "cmd": CMDS["PING"], "seq": 101}, ev_frame(CMDS["GET_STATUS"], 103)],
        "oversize frame followed directly by a valid one: the skip ends exactly after the oversize frame's CRC",
        [decoded(CMDS["PING"], 101, p1), decoded(CMDS["GET_STATUS"], 103)])
    f = frame(CMDS["PING"], 102, b"\x00")
    add("req_ping_1_byte", f, [ev_frame(CMDS["PING"], 102, b"\x00")], "", [decoded(CMDS["PING"], 102, b"\x00")])
    f = frame(CMDS["GET_STATUS"], 0xFFFF, b"")
    add("req_seq_ffff", f, [ev_frame(CMDS["GET_STATUS"], 0xFFFF)], "sequence number at the wrap", [decoded(CMDS["GET_STATUS"], 0xFFFF)])

    # 4. corruption
    good = frame(CMDS["GET_INFO"], 1)
    bad = bytearray(good); bad[15] ^= 0x01
    add("bad_header_crc", bytes(bad), [], "header CRC wrong: silently dropped, no answer")
    bad = bytearray(frame(CMDS["PING"], 2, b"hi")); bad[16] ^= 0x20
    add("bad_payload_data", bytes(bad), [{"event": "bad_crc", "cmd": CMDS["PING"], "seq": 2}],
        "payload byte flipped: device answers E_BAD_CRC with the request cmd/seq")
    bad = bytearray(frame(CMDS["PING"], 2, b"hi")); bad[-1] ^= 0x80
    add("bad_payload_crc_field", bytes(bad), [{"event": "bad_crc", "cmd": CMDS["PING"], "seq": 2}], "payload CRC field flipped")
    bad = bytearray(good); bad[1] = 0x14
    add("bad_magic", bytes(bad), [], "second magic byte wrong (0x14)")
    bad = bytearray(good); bad[0] = 0x5A
    add("bad_magic0", bytes(bad), [], "first magic byte wrong")
    f = frame(CMDS["GET_INFO"], 7, major=2)
    add("bad_version", f, [{"event": "bad_version", "cmd": CMDS["GET_INFO"], "seq": 7}],
        "proto_major 2: device answers E_VERSION", [])
    add("truncated_header", good[:10], [], "10 of 16 header bytes, then silence: discarded after 100 ms")
    f = frame(CMDS["PING"], 3, b"hello")
    add("truncated_payload", f[:-3], [], "header and part of payload/CRC, then silence: discarded after 100 ms")
    f = frame(CMDS["PING"], 4, b"x", flags=FLAG_RESPONSE)
    add("response_flag_to_device", f, [], "valid frame with RESPONSE set sent to the device: dropped",
        [decoded(CMDS["PING"], 4, b"x", flags=FLAG_RESPONSE)])

    # 5. stream behaviour
    f = frame(CMDS["GET_STATUS"], 20)
    add("junk_then_frame", b"AT\r\n\x00\xff\x13\xa5" + f, [ev_frame(CMDS["GET_STATUS"], 20)],
        "ModemManager style junk and a stray 0xA5 before a frame", [decoded(CMDS["GET_STATUS"], 20)])
    f1 = frame(CMDS["PING"], 21, b"abc"); f2 = frame(CMDS["GET_INFO"], 22)
    add("two_frames_back_to_back", f1 + f2, [ev_frame(CMDS["PING"], 21, b"abc"), ev_frame(CMDS["GET_INFO"], 22)],
        "one USB packet holding two frames", [decoded(CMDS["PING"], 21, b"abc"), decoded(CMDS["GET_INFO"], 22)])
    f = frame(CMDS["GET_INFO"], 23)
    add("resync_after_false_magic", MAGIC + f, [ev_frame(CMDS["GET_INFO"], 23)],
        "a5 13 then a real frame: the false header fails its CRC and parsing restarts after the bad magic",
        [decoded(CMDS["GET_INFO"], 23)])
    f = frame(CMDS["PING"], 24, MAGIC + b"\x01\x00")
    add("magic_inside_payload", f, [ev_frame(CMDS["PING"], 24, MAGIC + b"\x01\x00")],
        "payload containing the magic bytes is not a frame start", [decoded(CMDS["PING"], 24, MAGIC + b"\x01\x00")])

    # 5b. protocol 1.17 sequencer: requests besides the one-per-command set, and responses a host decodes
    p = struct.pack("<BBBBHHII", 2, 0, 0, 0, 1, 256, 0, 0)
    f = frame(CMDS["SET_SEQ"], 60, p)
    add("req_set_seq_average_256", f, [ev_frame(CMDS["SET_SEQ"], 60, p)],
        "1.17: hardware averaging, 1 position, N 256 (legacy records: N <= 257)", [decoded(CMDS["SET_SEQ"], 60, p)])
    p = struct.pack("<BBBBHHI", 1, 0x03, 0xFF, 0, 0, 1000, 0)
    f = frame(CMDS["READ_SEQ"], 61, p)
    add("req_read_seq_average_raw", f, [ev_frame(CMDS["READ_SEQ"], 61, p)],
        "1.17: averaged record, CH1 + CH2, the sums (S 0xFF), words 0..999", [decoded(CMDS["READ_SEQ"], 61, p)])
    p = struct.pack("<BBBBiII", 0x03, 3, 1, 0, (5 << 16) | 0, 120, 0)
    f = frame(CMDS["READ_RAW"], 62, p)
    add("req_read_raw_segment_5", f, [ev_frame(CMDS["READ_RAW"], 62, p)],
        "1.17: READ_RAW addr_mode 3: CH1 both cores interleaved, segment 5 (address 5 << 16 | 0), 120 words",
        [decoded(CMDS["READ_RAW"], 62, p)])
    gs = struct.pack("<BBBBHHII", 1, 0, 7, 0, 32, 0, 1000, 200) + struct.pack("<BBHHH", 0, 0, 16, 120, 64) + \
        struct.pack("<BBHIHBB", 0x81, 7, 0, 32, 1, 1, 0) + struct.pack("<IIHBB", 9, 32, 1, 1, 0)
    assert len(gs) == 48
    f = frame(CMDS["GET_SEQ"], 63, gs, flags=FLAG_RESPONSE)
    add("resp_get_seq_segments_done", f, [],
        "1.17 GET_SEQ response (48 bytes): 32 segments m 7, hold-off 1000 ns = 200 x 5 ns, fits (pre 16 / total 120 words, "
        "64 slots), FPGA 0x2A done + mode 1, 32 completed, 1 untriggered; held run = capture 9",
        [decoded(CMDS["GET_SEQ"], 63, gs, flags=FLAG_RESPONSE)])
    ent = [((1 << 63) | (1 << 53) | (((7 * s) % 128) << 40) | (20000 * s)) for s in range(2)]
    rs = struct.pack("<IHHBBBBHH", 9, 0, 2, 0, 0, 7, 0, 16, 120) + b"".join(struct.pack("<Q", e) for e in ent)
    f = frame(CMDS["READ_SEQ"], 64, rs, flags=FLAG_RESPONSE)
    add("resp_read_seq_table", f, [],
        "1.17 READ_SEQ table response: 16-byte header (capture 9, first 0, count 2, m 7, pre 16, total 120), then u64 "
        "entries: bits 39..0 timestamp in 5 ns (0, 100 us), 52..40 trigger offset in the slot (0, 7), 53 triggered, 63 valid",
        [decoded(CMDS["READ_SEQ"], 64, rs, flags=FLAG_RESPONSE)])
    st = bytes(44) + struct.pack("<HBB", 0x1F, 2, 6)
    f = frame(CMDS["GET_STATUS"], 65, st, flags=FLAG_RESPONSE)
    add("resp_get_status_1_17", f, [],
        "1.17 GET_STATUS tail (48 bytes; 0..43 as 1.13, zero here): 44 u16 features 0x1F (EXT, DECIM, FIR, SEGMENTS, "
        "HW_AVERAGE = FPGA 0x1536), 46 UI Average 2 = hardware, 47 log2 N = 6 (64 records per displayed record)",
        [decoded(CMDS["GET_STATUS"], 65, st, flags=FLAG_RESPONSE)])
    hb = bytearray(104)
    hb[0] = 1
    hb[2:4] = struct.pack("<H", 104)
    hb[88:104] = struct.pack("<BBBBIII", 2, 0x77, 0x05, 0xF6, 3, 2, 1200)
    hb = bytes(hb)
    f = frame(CMDS["GET_HEALTH"], 66, hb, flags=FLAG_RESPONSE)
    add("resp_get_health_1_19", f, [],
        "1.19 GET_HEALTH response (104 bytes; 4..87 as 1.16, zero here): 88 check mode 2 (FPGA read 0x33, bitstream "
        "0x1538), 89 last 0x33 = 0x77 (bit 0 failed, CH2 A pin wrong), 90 flags 0x05 (bit 0 read, bit 2 the last "
        "capture accepted garbled), 91 0x33 at boot 0xF6 (convention 1), 92 u32 bad 3, 96 u32 recovered 2, 100 u32 "
        "checked 1200",
        [decoded(CMDS["GET_HEALTH"], 66, hb, flags=FLAG_RESPONSE)])

    # 6. ASCII probe (section 10)
    add("ascii_ver", b"#VER\n", [{"event": "ascii", "line": "VER"}], "#VER probe")
    add("ascii_local_crlf", b"#LOCAL\r\n", [{"event": "ascii", "line": "LOCAL"}], "#LOCAL with CR LF (the LF is junk)")
    add("ascii_overflow", b"#" + b"X" * 70 + b"\n", [{"event": "ascii_overflow"}], "line over 63 bytes: #ERR,UNKNOWN")
    add("ascii_then_frame", b"#VER\r" + frame(CMDS["PING"], 25, b"z"),
        [{"event": "ascii", "line": "VER"}, ev_frame(CMDS["PING"], 25, b"z")], "", [decoded(CMDS["PING"], 25, b"z")])

    return {
        "format": "fnirsi-1013d protocol test vectors",
        "version": 1,
        "protocol": "1.0",
        "spec": "docs/protocol.md",
        "generator": "firmware/scope/host_tests/gen_vectors.py",
        "notes": [
            "hex: the byte stream fed to a parser in one go, followed by >= 100 ms of silence.",
            "frames: structurally valid frames (magic a5 13, header CRC-16/CCITT-FALSE, payload CRC-32/ISO-HDLC, "
            "proto_major 1) a direction-agnostic parser finds, responses included. The 492-byte request payload "
            "limit is a device rule and shows up only in device_events.",
            "device_events: what the scope parser reports, in order. event = frame | bad_crc | bad_len | "
            "bad_version | ascii | ascii_overflow. Dropped input produces no event. The device ignores frames "
            "with the RESPONSE flag.",
            "Field values: little-endian; payload layouts per docs/protocol.md section 8.",
        ],
        "crc_check": {"crc16_ccitt_false_123456789": "29b1", "crc32_iso_hdlc_123456789": "cbf43926"},
        "commands": {k: v for k, v in CMDS.items()},
        "decim": {
            "notes": "1.9 decimation filter: N = 2 * (floor(D / 2) + 1); effective K = 0 for k_req 0, divider 0 or "
                     "total > 8190, else the largest K <= k_req with 255 * M^K < 2^48; gain M^K; shift S = "
                     "max(0, bitlen(255 * gain) - 16) + fir; level16[i] = min(65535, round(levels[i] * gain / 2^S)); "
                     "rate = 200 MSa/s / N (K = 0: 200 MSa/s / (D + 1)); settle_us = ceil((K + 2) * N / 200); ext_shift = "
                     "round((120 ns + K (N - 1) 2.5 ns) / (N x 5 ns)), halves up, 0 for K = 0. "
                     "1.11 FIR (fir_req = SET_DECIM flags bit 3, FPGA 0x1535): fir = 1 when fir_req, K >= 1, N >= 16 "
                     "and N % 4 == 0; then M = N / 2 (else M = N), settle_us = ceil((K + 10) * N / 200) + 1, ext_shift = "
                     "round((250 ns + 17.5 N ns + 1.25 K (N - 2) ns) / (N x 5 ns)), halves up.",
            "levels": DECIM_LEVELS,
            "cases": decim_cases(),
        },
        "adc_cal_v3": adc_cal_v3_section(),
        "diag_layouts": diag_layouts_section(),
        "seq": {
            "notes": "1.17 sequencer (FPGA 0x1536), checked against pc_seq.c by seq_check (check_vectors.py --seq-checker). "
                     "fit: args mode flags m count n pre_words total_words wide -> fit code (0 ok, 1 mode, 2 flags, 3 m, "
                     "4 count, 5 total, 6 pre, 7 N), max count, table slots, 0x57, 0x58, 0x5A. Segments: m 5..12, "
                     "count <= 8192 >> m, total + 2 <= 2^m, pre < total; averaging: N 1..65536 (legacy <= 257), total "
                     "<= 2046, count x total <= 2048. hold: ns -> 5 ns units, rounded up. entry: u64 -> timestamp "
                     "(bits 39..0), offset (52..40), triggered (53), valid (63). segw: m s offset pre j -> sample memory "
                     "word s 2^m + ((offset - pre + j) mod 2^m). plan (UI averaging): total pre divider -> N (largest "
                     "power of two 2..64 with N (record + dead) <= 40 ms, word = 10 (D + 1) ns, dead = total x 5 + 40 + "
                     "(2 + pre) word + 100 ns; 0 = software) and the run in us, rounded up. avg: sum c gain off -> "
                     "corrected code of the mean: m8 = floor((sum x 256 + floor(c / 2)) / c), code = clamp((m8 x gain "
                     "+ off x 256 + 2^23) >> 24, 0, 255).",
            "cases": seq_cases(),
        },
        "vectors": vectors,
    }


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--output", default=os.path.join(here, "..", "..", "..", "docs", "protocol-vectors.json"))
    args = ap.parse_args()
    data = build()
    # Other generators merge their own vectors into the same file (mcp/src/fnirsi1013d/vectors.py --merge adds
    # entries with "source": "host"). Keep every vector that is not ours so regenerating never drops them.
    if os.path.exists(args.output):
        with open(args.output) as fh:
            old = json.load(fh)
        ours = {v["name"] for v in data["vectors"]}
        kept = [v for v in old.get("vectors", []) if v.get("source") not in (None, "firmware") and v["name"] not in ours]
        data["vectors"] += kept
        if kept:
            print(f"kept {len(kept)} vectors from other generators")
    with open(args.output, "w", newline="\n") as fh:
        json.dump(data, fh, indent=1)
        fh.write("\n")
    print(f"wrote {len(data['vectors'])} vectors to {os.path.normpath(args.output)}")


if __name__ == "__main__":
    main()
