"""tools/screenshots.py (the documentation site's screenshot tool) against the simulator: no hardware."""

from __future__ import annotations

import importlib.util
import struct
import sys
from pathlib import Path

import pytest

TOOL = Path(__file__).resolve().parents[2] / "tools" / "screenshots.py"


@pytest.fixture(scope="module")
def shots():
    if not TOOL.exists():  # the wheel/sdist test environments carry no tools/
        pytest.skip("tools/screenshots.py is not in this tree")
    spec = importlib.util.spec_from_file_location("screenshots_tool", TOOL)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = mod  # dataclasses with postponed annotations look their module up here
    try:
        spec.loader.exec_module(mod)
        yield mod
    finally:
        sys.modules.pop(spec.name, None)


def png_size(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    return struct.unpack(">II", data[16:24])


def test_list(shots, capsys):
    assert shots.main(["--list"]) == 0
    out = capsys.readouterr().out
    for name in ("scope", "frequency-response", "spectrum", "diagnostics"):
        assert name in out
    assert "manual" in out


def test_two_states_against_the_simulator(shots, tmp_path):
    rc = shots.main(["--simulator", "--only", "scope", "--only", "frequency-response", "--out", str(tmp_path)])
    assert rc == 0
    for name in ("scope", "frequency-response"):
        assert png_size(tmp_path / f"{name}.png") == (800, 480)


def test_unknown_state_is_refused(shots, tmp_path):
    with pytest.raises(SystemExit):
        shots.main(["--simulator", "--only", "nope", "--out", str(tmp_path)])
