"""Transport robustness and protocol 1.1 (firmware 0.1.1): resend on corrupted requests, resync on connect,
#RESET, GET_STATUS timing fields."""

from __future__ import annotations

import socket
import struct
import time

from fnirsi1013d import protocol as P
from fnirsi1013d.transport import TcpLink, Transport, connect


def test_request_resent_after_device_bad_crc(sim, transport):
    sim.bad_crc_next = 1
    assert transport.request(P.Cmd.PING, b"again").payload == b"again"
    assert transport.stats.retries == 1


def test_deferred_resent_after_device_bad_crc(sim_factory):
    srv = sim_factory()
    t = connect(srv.url)
    try:
        t.request(P.Cmd.SET_REMOTE, P.SetRemoteReq(1, 0, 0).pack())
        t.request(P.Cmd.SET_TRIGGER, P.SetTriggerReq(P.SetTriggerReq.M_MODE, mode=0).pack())
        t.request(P.Cmd.ARM, b"\0" * 4)
        srv.bad_crc_next = 1
        st = P.StatusResp.unpack(t.deferred(P.Cmd.WAIT, struct.pack("<I", 1000), 2.0).payload)
        assert st.acq_state == P.AcqState.DONE and t.stats.retries == 1
    finally:
        t.close()


def test_resync_flushes_dirty_device_ring(sim):
    """A previous session left a partial frame (header promising 300 bytes) in the device's RX ring."""
    link = TcpLink(sim.host, sim.port)
    link.write(P.Frame(P.Cmd.PING, 999, b"\x11" * 300).encode()[:40])
    t = Transport(link)  # resync on connect
    try:
        assert t.request(P.Cmd.PING, b"clean").payload == b"clean"
        assert t.stats.retries == 0
        assert P.InfoResp.unpack(t.request(P.Cmd.GET_INFO).payload).proto_minor == P.PROTO_MINOR
    finally:
        t.close()


def test_reset_line_inside_partial_frame(sim):
    s = socket.create_connection((sim.host, sim.port))
    try:
        s.sendall(P.Frame(P.Cmd.PING, 5, b"\x22" * 200).encode()[:60] + b"#RESET\n")
        s.settimeout(1)
        assert s.recv(64).startswith(b"#OK")
        s.sendall(P.Frame(P.Cmd.PING, 6, b"ok").encode())
        ev = [e for e in P.FrameParser().feed(s.recv(4096)) if isinstance(e, P.Frame)]
        assert ev[0].seq == 6 and ev[0].payload == b"ok"
    finally:
        s.close()


def test_reset_drops_pending_wait_keeps_remote(sim_factory):
    srv = sim_factory(signal="zero", legacy=True)
    t = connect(srv.url)
    try:
        t.request(P.Cmd.SET_REMOTE, P.SetRemoteReq(1, 0, 0).pack())
        t.request(P.Cmd.SET_TRIGGER, P.SetTriggerReq(P.SetTriggerReq.M_MODE | P.SetTriggerReq.M_LEVEL, level=250,
                                                     mode=1).pack())
        t.request(P.Cmd.ARM, b"\0" * 4)
        t.link.write(P.Frame(P.Cmd.WAIT, 0x7777, struct.pack("<I", 300)).encode())
        time.sleep(0.05)
        assert t.ascii("#RESET") == "#OK"
        time.sleep(0.4)  # the dropped WAIT never answers
        st = P.StatusResp.unpack(t.request(P.Cmd.GET_STATUS).payload)
        assert st.control == P.Control.REMOTE and not st.flags & P.StatusFlags.DEFERRED_PENDING
        assert t.stats.stale_responses == 0
        assert st.pass_ms == 5 and st.pass_max_ms == 5  # protocol 1.1 trailer
    finally:
        t.close()


def test_timeout_classes_follow_remote_state(legacy_transport):
    transport = legacy_transport
    from fnirsi1013d.transport import TimeoutPolicy

    pol = TimeoutPolicy()
    assert not transport.remote
    assert pol.for_cmd(P.Cmd.PING, False) == 0.1 and pol.for_cmd(P.Cmd.GET_TIMEBASE, False) == 1.0
    transport.request(P.Cmd.SET_REMOTE, P.SetRemoteReq(1, 0, 0).pack())
    assert transport.remote
    assert pol.for_cmd(P.Cmd.SET_TIMEBASE, True) == 0.3 and pol.for_cmd(P.Cmd.READ_RAW, True) == 0.5
    assert pol.for_cmd(P.Cmd.READ_RAW, False, shared=True) == 2.5  # 1.3 RUN-mode READ_RAW hold (<= 1 s)
    transport.request(P.Cmd.SET_REMOTE, P.SetRemoteReq(0, 0, 0).pack())
    assert not transport.remote


def test_fast_retry_after_lost_response(sim, transport):
    sim.drop_next = 1
    t0 = time.monotonic()
    assert transport.request(P.Cmd.PING, b"q").payload == b"q"
    assert time.monotonic() - t0 < 0.5 and transport.stats.retries == 1  # resent after ~100 ms, not 1 s


def test_get_link_stats_protocol_1_2(sim, scope):
    st = scope.link_stats()
    assert st is not None and list(st)[:4] == ["rx_packets", "rx_bytes", "rx_stalls", "rx_swept"]
    assert len(st) == len(P.LINK_STATS_NAMES) == 29  # 27 in fw 0.1.4..0.1.7, 29 from 0.2.0
    assert st["frames"] >= 2 and st["rx_bytes"] > 512 and st["responses"] >= 2 and st["dtr_up"] >= 1
    assert st["junk_bytes"] >= 512  # the resync preamble's zeros
    assert st["link_resets"] >= 1  # its #RESET
    assert P.parse_link_stats(P.pack_link_stats({"rx_bytes": 7})[:8]) == {"rx_packets": 0}  # short body


def test_host_keeps_partial_response_across_a_pause():
    """A response pausing > 100 ms mid-transfer must not be dropped by the host (census: garbage_bytes)."""
    import threading
    from fnirsi1013d.transport import Transport

    class SlowLink:
        description = "slow"

        def __init__(self):
            self.out = bytearray()
            self.q: list[bytes] = []
            self.lock = threading.Lock()

        def write(self, data):
            f = P.decode_frame(bytes(data)) if data[:2] == P.MAGIC else None
            if f is not None:
                resp = P.Frame(f.cmd, f.seq, f.payload, flags=1).encode()
                with self.lock:
                    self.q += [resp[:20], b"PAUSE", resp[20:]]

        def read(self, n, timeout):
            with self.lock:
                item = self.q.pop(0) if self.q else b""
            if item == b"PAUSE":
                time.sleep(0.3)
                return b""
            if not item:
                time.sleep(min(timeout, 0.01))
            return item

        def flush_input(self):
            pass

        def close(self):
            pass

    t = Transport(SlowLink(), resync=False, response_timeout=1.0)
    assert t.request(P.Cmd.PING, b"x" * 100).payload == b"x" * 100
    assert t.stats.partial_drops == 0 and t.stats.garbage_bytes == 0 and t.stats.retries == 0
