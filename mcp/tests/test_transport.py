"""Transport framing, flow control, retries and resync against the simulator."""

from __future__ import annotations

import socket
import struct
import time

import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d.transport import DeviceError, TcpLink, Transport, TransportError, connect, parse_ver, probe_ver


#: normal mode with a level the noise-only "zero" signal never reaches
NO_TRIGGER = P.SetTriggerReq(P.SetTriggerReq.M_MODE | P.SetTriggerReq.M_LEVEL, level=250, mode=1).pack()


def remote(t: Transport) -> None:
    t.request(P.Cmd.SET_REMOTE, P.SetRemoteReq(1, 0, 0).pack())


def test_ver_probe_and_info(sim, transport):
    line = transport.ascii("#VER")
    assert line.startswith("#OK,FNIRSI-1013D-AI,")
    v = parse_ver(line)
    assert v["proto"] == f"1.{P.PROTO_MINOR}" and v["fpga"] == "0x1532"
    info = P.InfoResp.unpack(transport.request(P.Cmd.GET_INFO).payload)
    assert info.ring_words == 8192 and info.max_total_samples == 16382
    assert transport.ascii("#WHAT") == "#ERR,UNKNOWN"
    assert transport.ascii("#" + "X" * 70) == "#ERR,UNKNOWN"


def test_probe_ver_on_raw_link(sim):
    link = TcpLink(sim.host, sim.port)
    try:
        assert probe_ver(link).startswith("#OK,FNIRSI-1013D-AI")
    finally:
        link.close()


def test_ping_echo_and_seq(transport):
    assert transport.stats.frames_sent == transport.stats.frames_received == 1  # the resync PING on connect
    for i in range(5):
        data = bytes([i]) * (i * 50)
        assert transport.request(P.Cmd.PING, data).payload == data
    assert transport.stats.frames_sent == transport.stats.frames_received == 6


def test_large_response_test_pattern(transport):
    for n in (492, 495, 496, 1008, 4076, 65536):
        data = transport.request(P.Cmd.TEST_PATTERN, P.TestPatternReq(n, 0).pack()).payload
        assert data == P.test_pattern_bytes(n, 0)
    assert transport.request(P.Cmd.TEST_PATTERN, P.TestPatternReq(1000, 3).pack()).payload == \
        P.test_pattern_bytes(1000, 3)


def test_not_remote_error_is_actionable(legacy_transport):
    transport = legacy_transport
    with pytest.raises(DeviceError) as e:
        transport.request(P.Cmd.ARM, b"\0" * 4)
    assert e.value.status == P.Status.E_NOT_REMOTE
    assert "REMOTE" in str(e.value)


def test_unknown_command_and_short_requests(transport):
    with pytest.raises(DeviceError) as e:
        transport.request(0x0777)
    assert e.value.status == P.Status.E_UNKNOWN_CMD
    # protocol 5 / 14.3: a shorter payload reads missing trailing fields as zero
    cfg = P.ChannelConfig.unpack(transport.request(P.Cmd.GET_CHANNEL, b"").payload)
    assert cfg.channel == 0
    remote(transport)
    before = transport.request(P.Cmd.GET_TRIGGER).payload
    assert transport.request(P.Cmd.SET_TRIGGER, b"\0\0").payload == before  # mask 0: nothing changes


def test_request_frame_limit_enforced_by_host(transport):
    with pytest.raises(TransportError, match="512"):
        transport.request(P.Cmd.PING, b"\0" * 493, retries=0)


def _recv_frames(s: socket.socket, n: int, timeout: float = 2.0) -> list[P.Frame]:
    p = P.FrameParser()
    got: list[P.Frame] = []
    deadline = time.monotonic() + timeout
    s.settimeout(0.5)
    while len(got) < n and time.monotonic() < deadline:
        try:
            got += [e for e in p.feed(s.recv(65536)) if isinstance(e, P.Frame)]
        except socket.timeout:
            pass
    return got


def test_oversize_request_gets_bad_len_and_stream_continues(sim):
    s = socket.create_connection((sim.host, sim.port))
    try:
        s.sendall(P.Frame(P.Cmd.PING, 1, b"\x55" * 600).encode() + P.Frame(P.Cmd.PING, 2, b"ok").encode())
        got = _recv_frames(s, 2)
        assert got[0].seq == 1 and got[0].status == P.Status.E_BAD_LEN
        assert got[1].seq == 2 and got[1].payload == b"ok"
    finally:
        s.close()


def test_bad_payload_crc_request_gets_e_bad_crc(sim):
    s = socket.create_connection((sim.host, sim.port))
    try:
        f = bytearray(P.Frame(P.Cmd.PING, 77, b"hello").encode())
        f[17] ^= 0xFF
        s.sendall(bytes(f))
        got = _recv_frames(s, 1)
        assert got[0].seq == 77 and got[0].status == P.Status.E_BAD_CRC
    finally:
        s.close()


def test_version_mismatch(sim):
    s = socket.create_connection((sim.host, sim.port))
    try:
        s.sendall(P.encode_frame(P.Cmd.GET_INFO, 5, proto_major=2))
        got = _recv_frames(s, 1)
        assert got[0].status == P.Status.E_VERSION
    finally:
        s.close()


def test_garbage_before_response_is_resynced(sim, transport):
    sim.garbage_next = b"AT\r\n\xa5\xa5\x13\x00junk"
    assert transport.request(P.Cmd.PING, b"x").payload == b"x"
    assert transport.stats.garbage_bytes > 0


def test_corrupted_response_is_retried(sim, transport):
    sim.corrupt_next = 1
    assert transport.request(P.Cmd.PING, b"retry-me").payload == b"retry-me"
    assert transport.stats.bad_payload_crc == 1 and transport.stats.retries == 1


def test_lost_response_retry_uses_duplicate_cache(sim, transport):
    """A retried ARM must not arm twice: the device answers from its last-response cache (section 4.3)."""
    remote(transport)
    transport.response_timeout = 0.2
    n0 = sim.device.arm_count
    sim.drop_next = 1
    st = P.StatusResp.unpack(transport.request(P.Cmd.ARM, b"\0" * 4).payload)
    assert st.control == P.Control.SHARED and st.acq_state == P.AcqState.DONE
    assert sim.device.arm_count == n0 + 1
    assert transport.stats.retries == 1


def test_recovery_after_repeated_loss(sim, transport):
    transport.response_timeout = 0.1
    sim.drop_next = 3  # first try + 2 retries lost -> recovery (PING) -> fresh try
    assert transport.request(P.Cmd.PING, b"r").payload == b"r"
    assert transport.stats.recoveries == 1


def test_partial_response_dropped_after_silence():
    p = P.FrameParser()
    full = P.Frame(P.Cmd.PING, 3, b"abcdef", flags=1).encode()
    assert p.feed(full[:10]) == []
    p.reset()  # what the transport does after 100 ms of silence
    ev = p.feed(full)
    assert isinstance(ev[0], P.Frame) and ev[0].payload == b"abcdef"


def test_deferred_wait_with_control_stop(sim_factory):
    srv = sim_factory(signal="zero")
    t = connect(srv.url)
    try:
        remote(t)
        t.request(P.Cmd.SET_TRIGGER, NO_TRIGGER)
        t.request(P.Cmd.ARM, b"\0" * 4)
        polled: list[P.StatusResp] = []
        t0 = time.monotonic()

        def cancel() -> bool:
            if not polled:
                polled.append(P.StatusResp.unpack(t.control(P.Cmd.GET_STATUS).payload))
            return time.monotonic() - t0 > 0.2
        with pytest.raises(DeviceError) as e:
            t.deferred(P.Cmd.WAIT, struct.pack("<I", 5000), 6.0, cancel=cancel)
        assert e.value.status == P.Status.E_ABORTED
        assert polled[0].flags & P.StatusFlags.DEFERRED_PENDING
        assert time.monotonic() - t0 < 2
        # a normal request while a deferred one is pending is refused by the host engine
        st = P.StatusResp.unpack(t.request(P.Cmd.GET_STATUS).payload)
        assert st.acq_state == P.AcqState.ABORTED
    finally:
        t.close()


def test_device_rejects_normal_request_during_deferred(sim_factory):
    srv = sim_factory(signal="zero")
    s = socket.create_connection((srv.host, srv.port))
    try:
        def xfer(frame: P.Frame, n: int = 1) -> list[P.Frame]:
            s.sendall(frame.encode())
            return _recv_frames(s, n)
        xfer(P.Frame(P.Cmd.SET_REMOTE, 1, P.SetRemoteReq(1, 0, 0).pack()))
        xfer(P.Frame(P.Cmd.SET_TRIGGER, 2, NO_TRIGGER))
        xfer(P.Frame(P.Cmd.ARM, 3, b"\0" * 4))
        s.sendall(P.Frame(P.Cmd.WAIT, 4, struct.pack("<I", 400)).encode())
        busy = xfer(P.Frame(P.Cmd.GET_TRIGGER, 5))
        assert busy[0].seq == 5 and busy[0].status == P.Status.E_BUSY
        ping = xfer(P.Frame(P.Cmd.PING, 6, b"c"))  # control request allowed
        assert ping[0].seq == 6 and ping[0].status == 0
        # a retransmitted WAIT (same seq) is ignored; the timeout answer arrives once
        s.sendall(P.Frame(P.Cmd.WAIT, 4, struct.pack("<I", 400)).encode())
        w = _recv_frames(s, 1)
        assert [f.seq for f in w] == [4]
        st = P.StatusResp.unpack(w[0].payload)
        assert w[0].status == 0 and st.acq_state == P.AcqState.ARMED  # timed out, no trigger
        assert _recv_frames(s, 1, timeout=0.3) == []
    finally:
        s.close()


def test_host_refuses_normal_request_while_deferred_pending(sim_factory):
    srv = sim_factory(signal="zero")
    t = connect(srv.url)
    try:
        remote(t)
        t.request(P.Cmd.SET_TRIGGER, NO_TRIGGER)
        t.request(P.Cmd.ARM, b"\0" * 4)
        errors: list[Exception] = []

        def cancel() -> bool:
            if not errors:
                try:
                    t.request(P.Cmd.GET_TRIGGER)
                except TransportError as e:
                    errors.append(e)
            return True
        with pytest.raises(DeviceError):
            t.deferred(P.Cmd.WAIT, struct.pack("<I", 2000), 3.0, cancel=cancel)
        assert errors and "deferred" in str(errors[0])
    finally:
        t.close()


def test_connect_rejects_bad_url():
    with pytest.raises(TransportError):
        connect("tcp://nohost")


class _ScriptedLink:
    """A link that answers by script: on each written frame, the callback returns bytes to deliver."""

    description = "scripted"

    def __init__(self, react):
        self.react = react
        self.rx = bytearray()
        self.sent: list[P.Frame] = []
        self.parser = P.FrameParser()

    def write(self, data: bytes) -> None:
        for ev in self.parser.feed(data):
            if isinstance(ev, P.Frame):
                self.sent.append(ev)
                self.rx += self.react(ev)

    def read(self, max_bytes: int, timeout: float) -> bytes:
        out, self.rx = bytes(self.rx[:max_bytes]), self.rx[max_bytes:]
        return out

    def flush_input(self) -> None:
        self.rx.clear()

    def close(self) -> None:
        pass


def test_cancelled_deferred_keeps_the_first_answer_and_never_resends():
    """CI flake (slow runner): after a one-shot cancel the host must collect E_ABORTED, never resend the WAIT,
    and a late second answer to the same seq must not overwrite the first."""
    status_body = P.StatusResp(control=2, acq_state=P.AcqState.ABORTED).pack()
    pending: dict[str, P.Frame] = {}

    def react(f: P.Frame) -> bytes:
        if f.cmd == P.Cmd.WAIT:
            pending["wait"] = f
            return b""
        if f.cmd == P.Cmd.SET_ACQ_STATE:
            w = pending["wait"]
            return (P.response(f, status_body).encode() +
                    P.response(w, P.error_payload(0, "aborted"), P.Status.E_ABORTED).encode() +
                    P.response(w, status_body).encode())  # a late duplicate answer
        return b""
    link = _ScriptedLink(react)
    t = Transport(link, resync=False)
    sent = []

    def press_run() -> bool:
        if not sent:
            sent.append(t.control(P.Cmd.SET_ACQ_STATE, P.SetAcqStateReq(1).pack()))
            return True
        return False
    resp = t.deferred(P.Cmd.WAIT, P.U32Req(2000).pack(), 3.0, cancel=press_run, on_cancel=lambda: None, check=False)
    assert resp.status == P.Status.E_ABORTED
    assert [f.cmd for f in link.sent].count(P.Cmd.WAIT) == 1  # not resent after the cancel
    t._pump(0.01)
    assert t.stats.stale_responses == 1  # the duplicate answer was dropped, not delivered
