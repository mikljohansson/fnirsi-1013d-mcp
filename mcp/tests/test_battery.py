"""fnirsi1013d-battery against the in-process simulator (``--sim``): the tool drives the real server over stdio and
judges every step; on the simulator nothing may FAIL (SKIP where the model is known to differ from the unit).
A smoke test so the battery cannot rot; the full set of groups runs in about 90 s, this subset in a fraction."""

from __future__ import annotations

import json

import numpy as np
import pytest

from fnirsi1013d import battery as B

SMOKE_GROUPS = "session,config,rates,acq,trigger,counter,gen,data,coreorder,final"


def test_battery_on_the_simulator(tmp_path, capsys):
    rc = B.main(["--sim", "--groups", SMOKE_GROUPS, "--coreorder-captures", "3", "--out", str(tmp_path)])
    out = capsys.readouterr().out
    rec = json.loads((tmp_path / "battery.json").read_text())
    bad = [f"{s['label']} {s['tool']}: {s['detail']}" for s in rec["steps"] if s["status"] == "FAIL"]
    assert rc == 0 and not bad, bad
    assert rec["counts"]["PASS"] > 150
    labels = {s["label"]: s for s in rec["steps"]}
    # the findings of 2026-11-06 are judged: the FFT / DVM rate, the counter's first reading, the frequency summary
    assert labels["A6"]["status"] == "PASS" and labels["A8"]["status"] == "PASS"
    assert labels["R0b"]["status"] == "PASS" and labels["G2a"]["status"] == "PASS"
    assert labels["A17"]["status"] == "SKIP"  # scope_spectrum needs the Spectrum view
    assert "PASS [S1] scope_info" in out
    md = (tmp_path / "battery.md").read_text()
    assert "**MCP battery**" in md and "| acq |" in md


def test_battery_fails_and_exits_nonzero(tmp_path):
    """A judged step that does not hold is a FAIL and the exit status says so: the generator stays off on the
    simulator when the pin is the EXT input, so the counter steps cannot count it."""
    rc = B.main(["--sim", B.SIM_ARGS.replace("--ext gen", "--ext zero"), "--groups", "counter", "--out",
                 str(tmp_path)])
    rec = json.loads((tmp_path / "battery.json").read_text())
    assert rc == 1
    assert any(s["label"] == "K4" and s["status"] == "FAIL" for s in rec["steps"])


def test_core_shift_measure():
    """core_order.py's measure: core B late by one conversion reads s = +1, in order s = 0."""
    t = np.arange(4000) * 0.5  # interleaved sample times in words
    edge = 40.0 * np.clip(np.mod(t, 100.0) - 50.0, 0, 3) / 3  # a 3-word ramp of 40 codes every 100 words
    v = 100 + edge
    b_, a_ = v[0::2], v[1::2]  # B half a word before A
    m, n = B.core_shift_rms(a_, b_)
    assert n > 10 and min(m, key=lambda k: m[k]) == 0
    m, _ = B.core_shift_rms(a_, np.roll(b_, 1))
    assert min(m, key=lambda k: m[k]) == 1


def test_list_and_bad_group(capsys):
    assert B.main(["--list"]) == 0
    assert "coreorder" in capsys.readouterr().out
    with pytest.raises(SystemExit):
        B.main(["--sim", "--groups", "nope"])


def test_battery_stops_at_once_without_a_scope(capsys, tmp_path):
    """No scope on the port: one message and exit status 2, not a FAIL line per step (bench 2026-11-06)."""
    from fnirsi1013d import battery

    assert battery.main(["--port", "tcp://127.0.0.1:9", "--groups", "session", "--out", str(tmp_path)]) == 2
    err = capsys.readouterr().err
    assert "cannot run" in err and "scope_info" in err
    assert not (tmp_path / "battery.md").exists()

