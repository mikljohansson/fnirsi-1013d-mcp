"""SerialLink must never reconfigure the port after opening (Windows usbser.sys / firmware 0.1.0 finding)."""

from __future__ import annotations

import serial

from fnirsi1013d import transport as T


class FakeSerial:
    """Records every setting change after open(); serves bytes from a buffer."""

    def __init__(self, port=None, **kw):
        self._settings = dict(kw)
        self.port = port
        self.dtr = self.rts = False
        self.is_open = False
        self.changes_after_open: list[str] = []
        self.buf = bytearray()
        self.reads: list[int] = []

    def __setattr__(self, name, value):
        if name in ("timeout", "baudrate", "rtscts", "dsrdtr", "xonxoff", "write_timeout") and \
                getattr(self, "is_open", False):
            self.changes_after_open.append(name)
        object.__setattr__(self, name, value)

    def open(self):
        self.is_open = True

    @property
    def in_waiting(self):
        return len(self.buf)

    def read(self, n):
        self.reads.append(n)
        out = bytes(self.buf[:n])
        del self.buf[:n]
        return out

    def write(self, data):
        return len(data)

    def reset_input_buffer(self):
        self.buf.clear()

    def close(self):
        self.is_open = False


def test_serial_link_never_reconfigures(monkeypatch):
    monkeypatch.setattr(serial, "Serial", FakeSerial)
    link = T.SerialLink("COM99")
    fake = link._ser
    assert fake._settings["timeout"] == T.SERIAL_POLL_S
    assert fake._settings["rtscts"] is False and fake._settings["dsrdtr"] is False
    assert fake.dtr is True and fake.is_open
    assert link.read(100, 0.05) == b""  # nothing waiting: polls with the fixed timeout until the deadline
    fake.buf += b"x" * 10000
    assert link.read(65536, 1.0) == b"x" * 10000  # asks only for what is waiting
    assert max(fake.reads) == 10000
    fake.buf += b"abc"
    assert link.read(2, 1.0) == b"ab"
    link.flush_input()
    link.close()
    assert fake.changes_after_open == []
