"""fnirsi1013d-battery against the in-process simulator (``--sim``): the tool drives the real server over stdio and
judges every step; on the simulator nothing may FAIL (SKIP where the model is known to differ from the unit).
A smoke test so the battery cannot rot; the full set of groups runs in about 90 s, this subset in a fraction."""

from __future__ import annotations

import json

import numpy as np
import pytest

from fnirsi1013d import analysis as A
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
    assert labels["T10b"]["status"] == "PASS"  # the Nth edge with an idle time waits for a gap, as on the unit
    assert labels["A17"]["status"] == "SKIP"  # scope_spectrum needs the Spectrum view
    # the ladder wiring: CH1 on its input judged at every divider, nothing of 499 kHz on the second node
    assert all(labels[f"O{d}b"]["status"] == "PASS" for d in B.CORE_ORDER_DIVIDERS)
    assert labels["O0c"]["status"] == "SKIP" and "second node" in labels["O0c"]["detail"]
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


@pytest.mark.parametrize("divider", [0, 1, 9, 999])
@pytest.mark.parametrize("delay", [1, 3, -1])
def test_fold_finds_the_core_delay(divider, delay):
    """core_delay_fold.py's measure: B[k] = f(2 (D + 1) k), A[k] = f(2 (D + 1) k + d (D + 1)), any divider."""
    period, step = 401, divider + 1
    k = np.arange(1500) * 2 * step

    def f(t):
        return np.where(np.mod(t, period) < 200, 170.0, 90.0)
    rng = np.random.default_rng(divider)
    b_ = np.rint(f(k) + rng.normal(0, 1, k.size))
    a_ = np.rint(f(k + delay * step) + rng.normal(0, 1, k.size))
    m = A.core_delay_fold(a_, b_, divider, period)
    assert min(m, key=lambda d: m[d]) == delay


def test_fold_verdict_reads_means_and_votes():
    recs = [{0: 1.0, 1: 0.9, 3: 2.0}, {0: 1.0, 1: 0.8, 3: 2.0}, {0: 0.9, 1: 1.0, 3: 2.0}]
    best, votes, mean = A.core_delay_verdict(recs)
    assert best == 1 and votes == {0: 1, 1: 2} and mean[3] == 2.0


def test_coreorder_both_probes_on_the_post(tmp_path):
    """The sharp wiring: both channels judged at every divider, no skip."""
    rc = B.main(["--sim", "--ch1 gen:high=3.1,offset=-1.24 --ch2 gen:high=3.1,offset=-1.24", "--groups", "config,coreorder",
                 "--coreorder-captures", "2", "--out", str(tmp_path)])
    rec = json.loads((tmp_path / "battery.json").read_text())
    steps = {s["label"]: s for s in rec["steps"]}
    assert rc == 0 and rec["counts"]["FAIL"] == 0 and rec["counts"]["SKIP"] == 0, rec["counts"]
    for d in B.CORE_ORDER_DIVIDERS:
        assert steps[f"O{d}b"]["status"] == "PASS" and steps[f"O{d}c"]["status"] == "PASS"
    assert "O0c" in steps


def test_coreorder_fails_on_the_wrong_pairing(tmp_path):
    rc = B.main(["--sim", "--ch1 gen:high=3.1,offset=-1.24 --true-order a_first", "--groups", "config,coreorder",
                 "--coreorder-captures", "2", "--out", str(tmp_path)])
    rec = json.loads((tmp_path / "battery.json").read_text())
    assert rc == 1
    assert next(s for s in rec["steps"] if s["label"] == "O5b")["detail"].count("d = -1") == 1


def test_coreorder_stops_without_a_signal_on_ch1(tmp_path):
    rc = B.main(["--sim", "--ch1 zero --ch2 zero", "--groups", "config,coreorder", "--out", str(tmp_path)])
    rec = json.loads((tmp_path / "battery.json").read_text())
    steps = {s["label"]: s for s in rec["steps"]}
    assert rc == 1 and steps["O0s"]["status"] == "FAIL" and steps["O0c"]["status"] == "SKIP" and "O5b" not in steps


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

