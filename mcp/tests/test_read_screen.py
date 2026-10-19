"""READ_SCREEN (0x0401, cap SCREENSHOT; built in firmware 0.2.70): the wire format against the firmware's frame vectors
and the simulator's answers against the same checks as firmware/scope/host_tests/test_session.c test_read_screen."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np
import pytest

from fnirsi1013d import protocol as P
from fnirsi1013d.scope import Scope
from fnirsi1013d.transport import DeviceError

VECTORS = json.loads((Path(__file__).resolve().parents[2] / "docs" / "protocol-vectors.json").read_text())


def vector_frame(name: str) -> dict:
    return next(x for x in VECTORS["vectors"] if x["name"] == name)["frames"][0]


def test_firmware_vectors_decode():
    f = vector_frame("resp_read_screen_last_row")
    body = bytes.fromhex(f["payload"])
    h = P.ReadScreenHeader.unpack(body)
    assert (h.width, h.row, h.nrows, h.format) == (800, 479, 1, 1)
    assert len(body) == 8 + h.nrows * h.width * 2
    px = np.frombuffer(body, "<u2", h.nrows * h.width, 8)
    x = np.arange(800)
    assert np.array_equal(px, 0x8080 | (x & 0x7F) | ((x >> 7) << 12) | ((479 & 15) << 8))
    e = vector_frame("resp_read_screen_bad_arg")
    assert e["status"] == P.Status.E_BAD_ARG
    assert P.parse_error_payload(bytes.fromhex(e["payload"])) == (0, "rows outside 0..480")
    o = vector_frame("host_resp_read_screen_e_overflow")
    assert o["status"] == P.Status.E_OVERFLOW and P.parse_error_payload(bytes.fromhex(o["payload"]))[0] == 2
    rq = P.ReadScreenReq.unpack(bytes.fromhex(vector_frame("req_read_screen")["payload"]))
    assert (rq.row, rq.nrows) == (0, 40)


def test_simulator_matches_firmware(sim_factory):
    srv = sim_factory()
    with Scope.connect(srv.url) as s:
        assert s.profile().has(P.Caps.SCREENSHOT)
        body = s.t.request(P.Cmd.READ_SCREEN, P.ReadScreenReq(479, 1).pack()).payload
        assert body[:8] == bytes.fromhex(vector_frame("resp_read_screen_last_row")["payload"])[:8]
        assert len(body) == 8 + 1600
        assert len(s.t.request(P.Cmd.READ_SCREEN, P.ReadScreenReq(439, 41).pack()).payload) == 8 + 41 * 1600
        for row, n in ((480, 1), (0, 0), (470, 11), (0xFFFF, 2)):
            with pytest.raises(DeviceError) as e:
                s.t.request(P.Cmd.READ_SCREEN, P.ReadScreenReq(row, n).pack())
            assert e.value.status == P.Status.E_BAD_ARG
        with pytest.raises(DeviceError) as e:
            s.t.request(P.Cmd.READ_SCREEN, P.ReadScreenReq(0, 42).pack())
        assert (e.value.status, e.value.detail) == (P.Status.E_OVERFLOW, 2)
        assert s.read_screen().shape == (480, 800)
