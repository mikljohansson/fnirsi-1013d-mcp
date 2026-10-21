"""Result presentation (fnirsi1013d.report, the scope_report tool): a PNG and a self-contained HTML page for every
kind of saved result, the no-matplotlib fallback, the CLI."""

from __future__ import annotations

import re
from pathlib import Path

import anyio
import numpy as np
import pytest

from fnirsi1013d import report as R
from fnirsi1013d.simulator import parse_signal
from test_server import client_for, setup_decay

PNG_MAGIC = b"\x89PNG\r\n\x1a\n"


def _offline(page: str) -> None:
    """Self-contained: no scripts, no external resources of any kind."""
    assert "<script" not in page.lower()
    assert not re.search(r"""(src|href)\s*=\s*["']?(https?:)?//""", page, re.I)
    assert "@import" not in page and "url(" not in page


def _check(res: dict, kind: str, *, words: tuple[str, ...] = ()) -> str:
    assert res["kind"] == kind
    png, page_path = Path(res["png"]), Path(res["html"])
    assert png.read_bytes()[:8] == PNG_MAGIC and png.stat().st_size > 5000
    page = page_path.read_text(encoding="utf-8")
    _offline(page)
    assert "data:image/png;base64," in page and "<table>" in page and "How this was measured" in page
    assert res["command"].startswith(R.KINDS[kind][1]) and res["command"] in page.replace("&#x27;", "'")
    for w in words:
        assert w in page, w
    return page


def test_reports_for_every_kind(sim_factory, tmp_path, monkeypatch):
    home = tmp_path / "home"
    monkeypatch.setenv("FNIRSI_MCP_HOME", str(home))
    srv = sim_factory(signal="decay:tau=80us,amp=0.3,period=1ms,width=200us", noise_lsb=1.0)
    seg = sim_factory(signal="square:freq=10kHz,amp=0.3")
    bode = sim_factory(noise_lsb=0.5)
    bode.device.cfg.signals = (parse_signal("dut_in:r=1.6k,c=1n"), parse_signal("dut_out:r=1.6k,c=1n"))

    async def main():
        async with client_for(srv, tmp_path / "a") as c:
            await setup_decay(c)
            cap = await c.call("scope_capture", channels=["CH1", "CH2"])
            r = await c.call("scope_report", source=cap["file"], command="scope_capture(channels=['CH1', 'CH2'])")
            page = _check(r, "capture", words=("CH1", "CH2", "Vpp"))
            assert r["png"].startswith(str(home / "reports")) and r["how_to_show"][0].startswith("Claude Code: Read")
            assert "the scope's 8-bit" in page or r["caveats"]  # the recorded scaling caveat
            avg = await c.call("scope_averaged_capture", n=4, channels=["CH1"], reject_clipped=False)
            _check(await c.call("scope_report", source=avg["file"]), "averaged_capture", words=("4 captures averaged",))
            dec = await c.call("scope_decay_fit", channel="CH1", edge_channel="CH2", t_start="5us", t_end="400us",
                               model="exp+offset", n_avg=4)
            r = await c.call("scope_report")  # 'latest' = the decay fit just saved
            page = _check(r, "decay_fit", words=("tau", "r2"))
            assert any(row["parameter"] == "tau" for row in r["table"])
            assert Path(r["source"]) == Path(dec["file"])
            fft = await c.call("scope_capture_and_fft", channel="CH2", n_avg=2)
            r = await c.call("scope_report", source=fft["file"], caveats=["the person asked for 1 kHz"])
            page = _check(r, "fft", words=("dBc", "the person asked for 1 kHz"))
            srv.device.ui_set_spectrum(True, channel=0)
            sp = await c.call("scope_spectrum", channel="CH1")
            _check(await c.call("scope_report", source=sp["file"], title="Spectrum view"), "spectrum",
                   words=("Spectrum view",))
            srv.device.ui_set_spectrum(False)
            assert "no such file" in await c.fails("scope_report", source="nope.npz")
        async with client_for(seg, tmp_path / "b") as c:
            await c.call("scope_set_timebase", sample_rate="200MS/s")
            await c.call("scope_set_trigger", source="CH1", edge="rising", level=0.0, mode="normal",
                         pretrigger_samples=32, total_samples=240)
            r = await c.call("scope_capture_segments", count=8, channels=["CH1"], holdoff="1us")
            rep = await c.call("scope_report", source=r["file"])
            _check(rep, "segments", words=("8 segments", "CH1 Vpp"))
            assert len(rep["table"]) == 8
        async with client_for(bode, tmp_path / "c") as c:
            for where in ("host", "device"):
                args = dict(start="30kHz", stop="1MHz", points=6, settle="fast", where=where)
                t = await c.call("scope_bode_sweep", **args)
                b = await c.call("scope_bode_sweep", **args, confirm_token=t["token"])
                rep = await c.call("scope_report", source=b["files"]["npz"])
                page = _check(rep, "bode", words=("-3 dB at", "|H| dB", "shape: low-pass"))
                assert len(rep["table"]) == 6 and "where=" in rep["command"]
                if where == "device":
                    assert "measured by the scope&#x27;s own Bode engine" in page
                rep_js = await c.call("scope_report", source=b["files"]["json"])  # the .json beside it works too
                assert rep_js["kind"] == "bode"
    anyio.run(main)


def test_without_matplotlib_the_page_has_the_numbers(tmp_path, monkeypatch):
    t = np.arange(1000) * 1e-6
    p = tmp_path / "x.npz"
    np.savez_compressed(p, meta_json=np.array('{"dt": 1e-6, "capture_id": 3, "triggered": true}'), t=t,
                        CH1_volts=np.sin(2 * np.pi * 1e4 * t).astype(np.float32))
    monkeypatch.setattr(R, "have_matplotlib", lambda: False)
    res = R.build_report(p, out_dir=tmp_path / "out")
    assert res["png"] is None and "matplotlib is not installed" in res["plot_note"]
    page = Path(res["html"]).read_text(encoding="utf-8")
    _offline(page)
    assert "matplotlib is not installed" in page and "data:image" not in page
    assert res["rows"][0]["frequency"] == "10 kHz" and res["rows"][0]["Vpp"].endswith("V")
    assert "arguments not recorded" in res["command"]


def test_cli_and_errors(tmp_path, capsys):
    d = tmp_path / "data" / "2026-10-19"
    d.mkdir(parents=True)
    t = np.arange(500) * 1e-7
    np.savez_compressed(d / "120000_000_capture_c1.npz", meta_json=np.array('{"dt": 1e-7}'), t=t,
                        CH1_volts=np.cos(2 * np.pi * 1e5 * t))
    assert R.main(["latest", "--data-dir", str(tmp_path / "data"), "--out-dir", str(tmp_path / "o"),
                   "--caveat", "probe x10"]) == 0
    out = capsys.readouterr().out
    assert "html:" in out and (tmp_path / "o" / "120000_000_capture_c1.html").is_file()
    assert "probe x10" in (tmp_path / "o" / "120000_000_capture_c1.html").read_text(encoding="utf-8")
    assert R.main(["latest", "--data-dir", str(tmp_path / "empty")]) == 2
    bad = tmp_path / "bad.npz"
    np.savez(bad, foo=np.zeros(3))
    with pytest.raises(R.ReportError, match="unknown result"):
        R.load_result(bad)
