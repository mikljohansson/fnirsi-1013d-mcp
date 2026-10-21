"""Protocol test vectors shared with the firmware: docs/protocol-vectors.json.

The file is owned jointly. The firmware agent generates the base set with
``firmware/scope/host_tests/gen_vectors.py``; this module adds host-side vectors (names prefixed ``host_``,
``"source": "host"``) with ``--merge``, which keeps every existing vector and adds only new names (the union).

Schema (defined by the firmware generator, see the file's ``notes``):

- ``hex``: the byte stream fed to a parser in one go, followed by >= 100 ms of silence;
- ``frames``: structurally valid frames (proto_major 1, both CRCs good) a direction-agnostic parser finds, with
  ``cmd seq status flags len payload``;
- ``device_events``: what the scope's request parser reports, in order: ``frame`` (with the fields above),
  ``bad_crc`` / ``bad_len`` / ``bad_version`` (with ``cmd seq``), ``ascii`` (with ``line`` without the
  ``#``), ``ascii_overflow``. Dropped input (bad header CRC, junk, RESPONSE-flagged frames, partial frames)
  produces no event.

``host_events`` / ``device_events`` below reproduce both views with :class:`fnirsi1013d.protocol.FrameParser`
so the tests can check the Python side against the same file as the C parser.
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path
from typing import Any

from . import protocol as P
from .paths import checkout_path

#: docs/protocol-vectors.json of the checkout this package runs from, None for an installed wheel (pass --file)
DEFAULT_PATH: Path | None = checkout_path("docs", "protocol-vectors.json")


def _decoded(f: P.Frame) -> dict[str, Any]:
    return {"cmd": f.cmd, "seq": f.seq, "status": f.status, "flags": f.flags, "len": len(f.payload),
            "payload": f.payload.hex()}


def host_frames(data: bytes) -> list[dict[str, Any]]:
    """Direction-agnostic view: every structurally valid proto-1 frame."""
    p = P.FrameParser(max_payload=P.MAX_RESPONSE_PAYLOAD, ascii_lines=True, max_ascii=P.MAX_REQUEST_FRAME)
    out = [_decoded(e) for e in p.feed(data) if isinstance(e, P.Frame) and e.proto_major == P.PROTO_MAJOR]
    p.reset()  # >= 100 ms of silence after the stream
    return out


def device_events(data: bytes) -> list[dict[str, Any]]:
    """The device's request parser (as in the simulator): 492-byte payload limit, 63-character ASCII lines."""
    p = P.FrameParser(max_payload=P.MAX_REQUEST_PAYLOAD, ascii_lines=True, max_ascii=63)
    out: list[dict[str, Any]] = []
    for e in p.feed(data):
        if isinstance(e, P.Frame):
            if e.is_response:
                continue
            if e.proto_major != P.PROTO_MAJOR:
                out.append({"event": "bad_version", "cmd": e.cmd, "seq": e.seq})
                continue
            out.append({**_decoded(e), "event": "frame"})
        elif isinstance(e, P.BadPayloadCrc):
            if e.frame.is_response:
                continue
            if e.frame.proto_major != P.PROTO_MAJOR:
                out.append({"event": "bad_version", "cmd": e.frame.cmd, "seq": e.frame.seq})
                continue
            out.append({"event": "bad_crc", "cmd": e.frame.cmd, "seq": e.frame.seq})
        elif isinstance(e, P.Oversize):
            if e.frame.is_response:
                continue
            if e.frame.proto_major != P.PROTO_MAJOR:
                out.append({"event": "bad_version", "cmd": e.frame.cmd, "seq": e.frame.seq})
                continue
            out.append({"event": "bad_len", "cmd": e.frame.cmd, "seq": e.frame.seq})
        elif isinstance(e, P.AsciiLine):
            out.append({"event": "ascii", "line": e.text[1:]})
        elif isinstance(e, P.AsciiOverflow):
            out.append({"event": "ascii_overflow"})
    p.reset()
    return out


def _f(cmd: int, seq: int, payload: bytes = b"", status: int = 0, flags: int = 0) -> P.Frame:
    return P.Frame(cmd, seq, payload, status, flags)


def _vec(name: str, data: bytes, description: str) -> dict[str, Any]:
    return {"name": name, "description": description, "hex": data.hex(), "frames": host_frames(data),
            "device_events": device_events(data), "source": "host"}


def build_host_vectors() -> list[dict[str, Any]]:
    """Vectors the host side adds: response payloads the host must decode and a few stream cases."""
    R = P.FLAG_RESPONSE
    v = []
    info = P.InfoResp(fpga_version=0x1532, fpga_variant=2, ring_words=8192, caps=0x1803, gen_clock_hz=50_000_000,
                      max_total_samples=16382, fw_version=b"0.1.0", fw_build=0x1234ABCD, serial=b"A1B2C3D4",
                      proto_minor=0)
    v.append(_vec("host_resp_get_info_al3", _f(P.Cmd.GET_INFO, 1, info.pack()[:76], flags=R).encode(),
                  "GET_INFO response, 76 bytes, AL3 0x1532, caps PROG_TRIGGER_POS|GENERATOR_DDS|READ_LAG_UNVERIFIED|"
                  "INTERLEAVE_UNVERIFIED"))
    st = P.StatusResp(1, 3, 0x0B, 0, 42, 6609, 6209, 3, 123456, 15, 18, 0)
    v.append(_vec("host_resp_get_status_done", _f(P.Cmd.GET_STATUS, 5, st.pack()[:28], flags=R).encode(),
                  "GET_STATUS response (protocol 1.0, 28 bytes): REMOTE, DONE, flags triggered|done|dtr, capture 42"))
    info11 = P.InfoResp(fpga_version=0x1532, fpga_variant=2, ring_words=8192, caps=0x1C03, gen_clock_hz=50_000_000,
                        max_total_samples=16382, fw_version=b"0.1.1", fw_build=0x5A8432BE, serial=b"1013D-0001",
                        proto_minor=1)
    v.append(_vec("host_resp_get_info_al3_proto_1_1", _f(P.Cmd.GET_INFO, 2, info11.pack()[:76], flags=R).encode(),
                  "GET_INFO response of firmware 0.1.1: proto 1.1, caps 0x1C03 (ENTER_FEL added)"))
    st11 = P.StatusResp(1, 3, 0x0B, 0, 43, 6609, 6209, 3, 123456, 15, 18, 0, 0, 7, 31)
    v.append(_vec("host_resp_get_status_proto_1_1", _f(P.Cmd.GET_STATUS, 6, st11.pack()[:32], flags=R).encode(),
                  "GET_STATUS response, protocol 1.1 (32 bytes): pass_ms 7, pass_max_ms 31"))
    v.append(_vec("host_ascii_reset", b"#RESET\n", "#RESET recovery line (firmware 0.1.1)"))
    v.append(_vec("host_resp_e_bad_crc", _f(P.Cmd.PING, 23, P.error_payload(0, "payload crc"), P.Status.E_BAD_CRC,
                                            R).encode(), "E_BAD_CRC answer with detail 0 and text"))
    v.append(_vec("host_resp_e_version", _f(P.Cmd.GET_INFO, 24, b"", P.Status.E_VERSION, R).encode(),
                  "E_VERSION answer without payload"))
    hdr = P.ReadRawHeader(42, 6209, 8192, 4, 0x0F, 0, 3, 0).pack()
    v.append(_vec("host_resp_read_raw_4x4", _f(P.Cmd.READ_RAW, 14, hdr + bytes(range(100, 116)), flags=R).encode(),
                  "READ_RAW response: 4 words x 4 cores, layout 0 (blocks CH1A, CH1B, CH2A, CH2B)"))
    v.append(_vec("host_req_read_raw_trigger_minus_400", _f(P.Cmd.READ_RAW, 15,
                  P.ReadRawReq(0x03, 2, 1, 0, -400, 1000).pack()).encode(),
                  "READ_RAW CH1 both cores, interleaved, 400 words before trigger_addr (i32 -400), 1000 words"))
    v.append(_vec("host_req_set_remote_release_keep", _f(P.Cmd.SET_REMOTE, 7, P.SetRemoteReq(0, 1, 0).pack()).encode(),
                  "release with KEEP_SETTINGS"))
    v.append(_vec("host_req_set_trigger_all_but_holdoff", _f(P.Cmd.SET_TRIGGER, 11,
                  P.SetTriggerReq(0x00BF, 1, 1, 160, 1, 800, 16000, 0, 0).pack()).encode(),
                  "mask 0xBF: CH2 falling level 160 normal, pretrigger 800, total 16000, auto_timeout 0"))
    v.append(_vec("host_req_wait_10000", _f(P.Cmd.WAIT, 13, struct.pack("<I", 10000)).encode(),
                  "WAIT with the maximum timeout 10000 ms"))
    three = _f(P.Cmd.PING, 40, b"a").encode() + b"\xa5\x13\x01\x00" + _f(P.Cmd.PING, 41).encode() + b"\xa5"
    v.append(_vec("host_frames_with_false_magic_and_tail", three,
                  "frame, a false 4-byte header start, frame, lone 0xA5 then silence (partial dropped)"))
    v.append(_vec("host_ascii_63_chars", b"#" + b"Y" * 63 + b"\n",
                  "longest accepted line: 63 characters after '#' (one more is ascii_overflow)"))
    v.append(_vec("host_req_read_screen_tail", _f(P.Cmd.READ_SCREEN, 42, P.ReadScreenReq(440, 40).pack()).encode(),
                  "READ_SCREEN rows 440..479 (the last of the 12 requests of a full screen)"))
    v.append(_vec("host_resp_read_screen_e_overflow", _f(P.Cmd.READ_SCREEN, 43,
                  P.error_payload(2, "at most 41 rows per request"), P.Status.E_OVERFLOW, R).encode(),
                  "READ_SCREEN E_OVERFLOW detail 2: more rows than fit one response (42 x 1600 + 8 > 66560)"))
    v += build_host_vectors_1_3()
    v += build_host_vectors_1_25()
    v += build_host_vectors_1_26()
    return v


def build_host_vectors_1_26() -> list[dict[str, Any]]:
    """Protocol 1.26 (FPGA 0x153C, docs/plans/fpga-0x153c.md; provisional numbering) frames: SET_TRIGGER2 for every
    type, GET_TRIGGER 52 bytes, GET_COUNTER read / configure and its 24-byte answer, SET_DECIM peak, GET_DECIM with
    the peak bits, READ_RAW layout 3 request and answer, GET_CAPTURE_INFO 96 bytes with byte 79 bit 1, GET_INFO with
    caps 20-22 and HOLDOFF for 0x153C, the mismatch refusal text of this generation."""
    R = P.FLAG_RESPONSE
    C = P.Caps
    caps = int(C.PROG_TRIGGER_POS | C.GENERATOR | C.HOLDOFF | C.EXT_TRIGGER | C.SCREENSHOT | C.ENTER_FEL |
               C.READ_LAG_UNVERIFIED | C.SHARED_MODE | C.SD_SECTORS | C.DECIM | C.FIR | C.SEQ | C.ENC_CHECK |
               C.CAPTURE_STAGES | C.TRIG_TYPES | C.PEAK_DETECT | C.COUNTER)  # 0x7FEC4F
    v = []
    info = P.InfoResp(fpga_version=0x153C, fpga_variant=2, ring_words=8192, caps=caps, gen_clock_hz=50_000_000,
                      max_total_samples=16382, fw_version=b"0.2.104", fw_build=0x0C8C0126, serial=b"1013D-0001",
                      proto_minor=26, fw_fpga_version=0x153C)
    v.append(_vec("host_resp_get_info_1_26", _f(P.Cmd.GET_INFO, 7, info.pack(), flags=R).encode(),
                  "GET_INFO response, protocol 1.26 (80 bytes): FPGA 0x153C, caps add HOLDOFF (2), TRIG_TYPES (20), "
                  "PEAK_DETECT (21), COUNTER (22) = 0x7FEC4F"))
    v.append(_vec("host_resp_e_unsupported_fpga_mismatch_1_26", _f(P.Cmd.ARM, 8, P.error_payload(
        P.UNSUP_FPGA_MISMATCH, "FPGA bitstream mismatch: this firmware needs 0x153C"), P.Status.E_UNSUPPORTED,
        R).encode(), "E_UNSUPPORTED detail 1 with the 1.26 mismatch text (the scope runs another word than 0x153C)"))
    # the firmware's vectors req_set_trigger2* / resp_get_trigger_1_26 (gen_vectors.py) have the same bytes as these
    # structs: tests/test_trig26.py checks Trigger2.pack() against them
    pw = P.Trigger2(P.TRIG_PULSE_WIDTH, 0, P.TRIG2_F_LONGER, 0, 1500, 0, 140, 0, 0, 0)
    v.append(_vec("host_req_set_trigger2_pulse_longer", _f(P.Cmd.SET_TRIGGER2, 50, pw.pack()).encode(),
                  "SET_TRIGGER2 0x0219 (24 bytes): type 1 pulse width, polarity 0 positive pulse, flags LONGER, "
                  "source CH1, N 1500 samples (15 us at 1 MSa/s legacy words), H 0, level 140, level2 0, hold-off 0"))
    nth = P.Trigger2(P.TRIG_NTH_EDGE, 1, 0, 1, 3, 20000, 128, 0, 0, 0)
    v.append(_vec("host_req_set_trigger2_nth_edge", _f(P.Cmd.SET_TRIGGER2, 51, nth.pack()).encode(),
                  "SET_TRIGGER2: type 2 Nth edge, polarity 1 falling edges counted, source CH2, N 3, idle H 20000 "
                  "samples, level 128"))
    to = P.Trigger2(P.TRIG_TIMEOUT, 1, 0, 2, 200000, 0, 0, 0, 0, 0)
    v.append(_vec("host_req_set_trigger2_timeout_ext", _f(P.Cmd.SET_TRIGGER2, 52, to.pack()).encode(),
                  "SET_TRIGGER2: type 3 timeout, polarity 1 held low, source EXT, N 200000 x 5 ns = 1 ms, level 0"))
    ho = P.Trigger2(P.TRIG_EDGE, 0, 0, 0, 0, 0, 128, 0, 500_000, 0)
    v.append(_vec("host_req_set_trigger2_edge_holdoff", _f(P.Cmd.SET_TRIGGER2, 53, ho.pack()).encode(),
                  "SET_TRIGGER2: type 0 edge rising (N, H 0), CH1, level 128, hold-off 500 000 x 5 ns = 2.5 ms (the "
                  "firmware writes 0x66 with it: the next ARM clears a running hold-off)"))
    trig = P.Trigger(source=0, edge=0, level=140, mode=1, pretrigger_samples=1000, total_samples=4000,
                     holdoff=500000, auto_timeout=4000, pretrigger_words=500, total_words=2000,
                     trigger2=P.Trigger2(**{**pw.__dict__, "holdoff": 500000}))
    v.append(_vec("host_resp_get_trigger_1_26_pulse", _f(P.Cmd.GET_TRIGGER, 54, trig.pack(), flags=R).encode(),
                  "GET_TRIGGER response, protocol 1.26 (52 bytes): the 28-byte TRIGGER (12 hold-off 500000 units) then "
                  "the SET_TRIGGER2 block at 28..51 (pulse width longer than 1500, level 140, hold-off 500000)"))
    st = P.StatusResp(control=2, acq_state=1, flags=0x08, capture_id=44, ms_since_arm=3, uptime_ms=123456,
                      battery_level=18, pass_ms=20, pass_max_ms=35, settings_seq=17, capture_settings_seq=16,
                      run_state=2, capture_origin=0, features=0x7FF, fpga_37=P.STATUS37_HOLDOFF_RUNNING,
                      fpga_34=P.STATUS34_COUNTING | P.TRIG_NTH_EDGE, edges_left=2)
    v.append(_vec("host_resp_get_status_1_26_armed", _f(P.Cmd.GET_STATUS, 55, st.pack(), flags=R).encode(),
                  "GET_STATUS response, protocol 1.26 (52 bytes): ARMED (run_state SINGLE), features 0x7FF, 48 0x37 = "
                  "0x10 hold-off running, 49 0x34 = 0x22 counting + type 2 Nth edge, 50 edges still to count 2"))
    v.append(_vec("host_req_get_counter_read", _f(P.Cmd.GET_COUNTER, 60, P.CounterReq().pack()).encode(),
                  "GET_COUNTER 0x0213 request (8 bytes): what 0 = read the snapshot (the other fields ignored)"))
    v.append(_vec("host_req_get_counter_config", _f(P.Cmd.GET_COUNTER, 61, P.CounterReq(
        P.CounterReq.CONFIG, 3, 2, 140, 6).pack()).encode(),
        "GET_COUNTER request: what 1 = configure then read: gate 3 (1 s), source 2 (CH2), level 140, hysteresis 6"))
    cnt = P.CounterResp(P.COUNTER_F_NEW | P.COUNTER_F_VALID, 17, 2, 0, 1000, 20_000_000, 4_000_000, 112, 147, 128, 6,
                        P.counter_freq_mhz(1000, 20_000_000))
    v.append(_vec("host_resp_get_counter", _f(P.Cmd.GET_COUNTER, 60, cnt.pack(), flags=R).encode(),
                  "GET_COUNTER response (24 bytes): flags NEW|VALID, seq 17, gate 2 (100 ms), source 0 (the trigger "
                  "source, CH1), E 1000, T 20 000 000 clocks, W 4 000 000 (10.000000 kHz, duty 20 %), min 112, max "
                  "147, level 128, hysteresis 6, freq_mHz 10 000 000"))
    v.append(_vec("host_req_set_decim_peak", _f(P.Cmd.SET_DECIM, 62, P.SetDecimReq(P.DECIM_PEAK, 0).pack()).encode(),
                  "SET_DECIM filter 4 = peak detect (flags 0)"))
    dec = P.decim_config(P.DECIM_PEAK, 0, 199, 4000, P.DECIM_HAS_FIR | P.DECIM_HAS_CAPSTAGES)
    ds = P.DecimState(P.DECIM_PEAK, 0, dec.status, dec.shift_byte, dec.n, dec.gain, 199, 1_000_000, 0xFF, 0, 0, 0,
                      P.DECIM_MAX_TOTAL, 0, 0, int(P.Features(0x7FF)), 0, 0)
    v.append(_vec("host_resp_get_decim_peak", _f(P.Cmd.GET_DECIM, 62, ds.pack(), flags=R).encode(),
                  "GET_DECIM / SET_DECIM response (36 bytes) with peak detect at divider 199: filter 4, next mode 0x81 "
                  "(16-bit, K 1), shift byte 0xC0 (bit 7 peak active, bit 6 peak written, S 0), N 200, gain 1, "
                  "1 MSa/s, features 0x7FF (bits 8-10 TRIG_TYPES / PEAK / COUNTER)"))
    v.append(_vec("host_req_read_raw_peak", _f(P.Cmd.READ_RAW, 63, P.ReadRawReq(0x03, 1, 3, 0, 0, 4).pack()).encode(),
                  "READ_RAW CH1 both cores, addr_mode 1 (capture-relative), layout 3 = peak pairs, 4 words"))
    hdr = P.ReadRawHeader(77, 6209, 8192, 4, 0x03, 3, 3, 0).pack()
    v.append(_vec("host_resp_read_raw_peak", _f(P.Cmd.READ_RAW, 63, hdr + bytes((100, 101, 90, 102, 140, 141, 170,
                                                                                  139)), flags=R).encode(),
                  "READ_RAW response, layout 3: per channel 4 minima (100 101 90 102) then 4 maxima (140 141 170 139)"))
    ci = P.CaptureInfo(
        P.CaptureInfoHeader(capture_id=77, settings_seq=20, origin=0, flags=0x01, trigger_addr=6609,
                            capture_start_word=6209, divider=199, timeperdiv_index=0xFF, trig_source=0, trig_edge=0,
                            trig_level=140, trig_mode=1, pretrigger_samples=1000, total_samples=4000),
        [P.CaptureChannelInfo(1, 1, 0, 0, 1, 1000, 3325 * 25, 32768),
         P.CaptureChannelInfo(1, 3, 0, 0, 1, 1010, 33250, 33024)],
        decim_mode=dec.status, decim_shift=dec.shift_byte, ext_shift=0, decim_n=dec.n, has_decim=True,
        run=P.SeqRunInfo(wide=P.CAPINFO_WIDE | P.CAPINFO_PEAK),
        trig2=P.CaptureTrig2(P.TRIG_PULSE_WIDTH, P.TRIG2_F_LONGER, 1500, 0, 500000))
    v.append(_vec("host_resp_get_capture_info_1_26_peak", _f(P.Cmd.GET_CAPTURE_INFO, 64, ci.pack(), flags=R).encode(),
                  "GET_CAPTURE_INFO response (112 bytes) of a peak-detect host capture at divider 199 with a pulse-width "
                  "trigger: byte 68 mode 0x81, 69 shift byte 0xC0, 72 N 200, 76 run mode 0 (a single capture), 79 = 0x03 "
                  "(bit 0 16-bit records, bit 1 peak), 96 type 1, 97 flags LONGER, 100 N 1500, 104 H 0, 108 hold-off "
                  "500000"))
    return v


def build_host_vectors_1_25() -> list[dict[str, Any]]:
    """Protocol 1.25 (fw 0.2.102, one built-for bitstream) frames: GET_INFO 80 bytes with and without the mismatch,
    the refusal every non-basic command gets on a mismatch."""
    R = P.FLAG_RESPONSE
    C = P.Caps  # the firmware's fixed word (pc_variant_init; READ_LAG_UNVERIFIED still set, INTERLEAVE_UNVERIFIED clear)
    caps = int(C.PROG_TRIGGER_POS | C.GENERATOR | C.EXT_TRIGGER | C.SCREENSHOT | C.ENTER_FEL | C.READ_LAG_UNVERIFIED |
               C.SHARED_MODE | C.SD_SECTORS | C.DECIM | C.FIR | C.SEQ | C.ENC_CHECK | C.CAPTURE_STAGES)  # 0xFEC4B
    v = []
    info = P.InfoResp(fpga_version=0x153B, fpga_variant=2, ring_words=8192, caps=caps, gen_clock_hz=50_000_000,
                      max_total_samples=16382, fw_version=b"0.2.102", fw_build=0x0A014F16, serial=b"1013D-0001",
                      proto_minor=25, fw_fpga_version=0x153B)
    v.append(_vec("host_resp_get_info_1_25", _f(P.Cmd.GET_INFO, 4, info.pack(), flags=R).encode(),
                  "GET_INFO response, protocol 1.25 (80 bytes): FPGA 0x153B = the built-for word at 76, flags 78 = 0"))
    bad = P.InfoResp(**{**info.__dict__, "fpga_version": 0x1532, "fpga_flags": P.FPGA_F_MISMATCH})
    v.append(_vec("host_resp_get_info_1_25_mismatch", _f(P.Cmd.GET_INFO, 5, bad.pack(), flags=R).encode(),
                  "GET_INFO response, protocol 1.25: the FPGA reports 0x1532, the firmware is built for 0x153B "
                  "(76), flags 78 bit 0 FPGA_MISMATCH; caps and geometry are the firmware's fixed ones"))
    v.append(_vec("host_resp_e_unsupported_fpga_mismatch", _f(P.Cmd.ARM, 6, P.error_payload(
        P.UNSUP_FPGA_MISMATCH, "FPGA bitstream mismatch: this firmware needs 0x153B"), P.Status.E_UNSUPPORTED,
        R).encode(),
        "E_UNSUPPORTED detail 1 with the mismatch text: every command outside the basic set on such a scope"))
    return v


def build_host_vectors_1_3() -> list[dict[str, Any]]:
    """Protocol 1.3 (collaborative mode) frames."""
    R = P.FLAG_RESPONSE
    v = []
    info = P.InfoResp(fpga_version=0x1532, fpga_variant=2, ring_words=8192, caps=0x3C03, gen_clock_hz=50_000_000,
                      max_total_samples=16382, fw_version=b"0.2.0", fw_build=0x0013C0DE, serial=b"1013D-0001",
                      proto_minor=3)
    v.append(_vec("host_resp_get_info_al3_proto_1_3", _f(P.Cmd.GET_INFO, 3, info.pack()[:76], flags=R).encode(),
                  "GET_INFO response, protocol 1.3: caps 0x3C03 (SHARED_MODE bit 13 added)"))
    st = P.StatusResp(control=2, acq_state=3, flags=0x0B, capture_id=44, trigger_addr=6609, capture_start_word=6209,
                      ms_since_arm=3, uptime_ms=123456, battery_level=18, pass_ms=20, pass_max_ms=35,
                      settings_seq=17, capture_settings_seq=16, run_state=0, capture_origin=0)
    v.append(_vec("host_resp_get_status_shared", _f(P.Cmd.GET_STATUS, 8, st.pack()[:44], flags=R).encode(),
                  "GET_STATUS response, protocol 1.3 (44 bytes): control 2 SHARED, host single DONE, capture 44, "
                  "settings_seq 17, capture_settings_seq 16, run_state STOP, capture_origin host"))
    v.append(_vec("host_req_set_acq_state_run", _f(P.Cmd.SET_ACQ_STATE, 30, P.SetAcqStateReq(1).pack()).encode(),
                  "SET_ACQ_STATE 0x0208 RUN (u8 state 1, 3 reserved)"))
    v.append(_vec("host_req_get_capture_info", _f(P.Cmd.GET_CAPTURE_INFO, 31).encode(),
                  "GET_CAPTURE_INFO 0x0209 request (empty)"))
    ci = P.CaptureInfo(
        P.CaptureInfoHeader(capture_id=45, settings_seq=17, origin=1, flags=0x01, trigger_addr=6609,
                            capture_start_word=6209, divider=19, timeperdiv_index=24, trig_source=1, trig_edge=1,
                            trig_level=160, trig_mode=1, pretrigger_samples=800, total_samples=16000),
        [P.CaptureChannelInfo(1, 5, 0, 0, 2, 1000, 3325, 32768),
         P.CaptureChannelInfo(1, 2, 1, 0, 1, 1010, 33250, 33024)])
    v.append(_vec("host_resp_get_capture_info", _f(P.Cmd.GET_CAPTURE_INFO, 31, ci.pack(), flags=R).encode(),
                  "GET_CAPTURE_INFO response (68 bytes): capture 45 from the UI RUN loop, settings_seq 17, triggered, "
                  "divider 19, CH2 falling 160 normal, 800/16000; CH1 range 5 DC 10x, CH2 range 2 AC 1x"))
    return v


def check_file(path: Path) -> list[str]:
    """Return mismatches between the Python parsers and the file."""
    doc = json.loads(path.read_text(encoding="utf-8"))
    errs = []
    for vec in doc["vectors"]:
        data = bytes.fromhex(vec["hex"])
        hf = host_frames(data)
        if hf != vec["frames"]:
            errs.append(f"{vec['name']}: host frames {hf} != {vec['frames']}")
        de = device_events(data)
        exp = [dict(sorted(e.items())) for e in vec["device_events"]]
        if [dict(sorted(e.items())) for e in de] != exp:
            errs.append(f"{vec['name']}: device events {de} != {vec['device_events']}")
    return errs


def main(argv: list[str] | None = None) -> None:
    ap = argparse.ArgumentParser(prog="python -m fnirsi1013d.vectors", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--file", type=Path, default=DEFAULT_PATH,
                    help="the vectors file (default: docs/protocol-vectors.json of the checkout, when run from one)")
    ap.add_argument("--merge", action="store_true", help="add host vectors with new names (keeps the rest)")
    ap.add_argument("--check", action="store_true", help="check the Python parsers against the file")
    a = ap.parse_args(argv)
    if a.file is None:
        ap.error("--file is required: no repository checkout around this package to take "
                 "docs/protocol-vectors.json from")
    if not a.file.is_file():
        ap.error(f"--file {a.file}: no such file")
    if a.merge:
        doc = json.loads(a.file.read_text(encoding="utf-8"))
        names = {x["name"] for x in doc["vectors"]}
        new = [x for x in build_host_vectors() if x["name"] not in names]
        doc["vectors"].extend(new)
        with open(a.file, "w", newline="\n", encoding="utf-8") as fh:
            json.dump(doc, fh, indent=1)
            fh.write("\n")
        print(f"added {len(new)} host vectors; {len(doc['vectors'])} total")
    if a.check or not a.merge:
        errs = check_file(a.file)
        for e in errs:
            print("FAIL:", e)
        print("ok" if not errs else f"{len(errs)} mismatches")
        raise SystemExit(1 if errs else 0)


if __name__ == "__main__":
    main()
