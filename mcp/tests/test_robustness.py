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
        assert P.InfoResp.unpack(t.request(P.Cmd.GET_INFO).payload).proto_minor == 1
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
    srv = sim_factory(signal="zero")
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
