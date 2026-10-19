"""The package outside a checkout: per-user home for the server's files, inputs by argument with clear errors.

The installed-wheel case is simulated by setting the module-level checkout defaults to None (what ``paths.checkout()``
gives in ``site-packages``); ``.github/scripts/wheel_smoke.py`` checks the real wheel in a fresh venv.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from fnirsi1013d import bench, flash, paths, sdcli, server, vectors


def test_checkout_detected_from_the_source_tree():
    root = paths.checkout()
    assert root is not None and (root / "mcp" / "src" / "fnirsi1013d" / "paths.py").is_file()
    assert paths.checkout_path("docs", "protocol-vectors.json") == root / "docs" / "protocol-vectors.json"
    assert paths.checkout_path("no", "such", "file") is None
    assert paths.checkout_dir("backups", "sdcard") == root / "backups" / "sdcard"


def test_not_a_checkout_when_the_layout_is_missing(monkeypatch, tmp_path):
    fake = tmp_path / "lib" / "site-packages" / "fnirsi1013d" / "paths.py"
    monkeypatch.setattr(paths, "__file__", str(fake))
    assert paths.checkout() is None and paths.checkout_path("docs") is None and paths.checkout_dir("x") is None


def test_server_defaults_to_the_user_home(monkeypatch, tmp_path):
    for k in ("FNIRSI_MCP_DATA_DIR", "FNIRSI_MCP_AUDIT_LOG", "FNIRSI_MCP_HOME"):
        monkeypatch.delenv(k, raising=False)
    monkeypatch.setattr(Path, "home", classmethod(lambda cls: tmp_path / "user"))
    cfg = server.ServerConfig.from_env()
    assert cfg.data_dir == tmp_path / "user" / ".fnirsi1013d" / "data"
    assert cfg.audit_log == tmp_path / "user" / ".fnirsi1013d" / "logs" / "audit.jsonl"
    monkeypatch.setenv("FNIRSI_MCP_HOME", str(tmp_path / "h"))
    cfg = server.ServerConfig.from_env()
    assert cfg.data_dir == tmp_path / "h" / "data" and cfg.audit_log == tmp_path / "h" / "logs" / "audit.jsonl"
    # --home beats FNIRSI_MCP_HOME; the specific variables beat --home; arguments beat everything
    cfg = server.ServerConfig.from_env(home=tmp_path / "arg")
    assert cfg.data_dir == tmp_path / "arg" / "data"
    monkeypatch.setenv("FNIRSI_MCP_DATA_DIR", str(tmp_path / "d"))
    monkeypatch.setenv("FNIRSI_MCP_AUDIT_LOG", "")
    cfg = server.ServerConfig.from_env(home=tmp_path / "arg")
    assert cfg.data_dir == tmp_path / "d" and cfg.audit_log is None
    cfg = server.ServerConfig.from_env(home=tmp_path / "arg", data_dir=tmp_path / "x")
    assert cfg.data_dir == tmp_path / "x"


def test_list_tools_needs_no_scope_and_creates_nothing(monkeypatch, tmp_path, capsys):
    monkeypatch.delenv("FNIRSI_MCP_DATA_DIR", raising=False)
    monkeypatch.delenv("FNIRSI_MCP_AUDIT_LOG", raising=False)
    monkeypatch.delenv("FNIRSI_MCP_MAX_TIER", raising=False)
    server.main(["--list-tools", "--home", str(tmp_path / "home"), "--port", "COM999"])
    out = capsys.readouterr().out
    assert "scope_status" in out and "scope_raw_fpga" not in out
    assert "tools (max tier 3)" in out and str(tmp_path / "home" / "data") in out
    assert not (tmp_path / "home").exists()
    tools = server.tool_list(server.ServerConfig(max_tier=4, enable_raw=True, data_dir=tmp_path, audit_log=None))
    assert {t for _, t, _ in tools} == {0, 1, 2, 3, 4} and "scope_raw_fpga" in {n for n, _, _ in tools}


def test_vectors_cli_requires_a_file_outside_a_checkout(monkeypatch, capsys, tmp_path):
    monkeypatch.setattr(vectors, "DEFAULT_PATH", None)
    with pytest.raises(SystemExit) as e:
        vectors.main(["--check"])
    assert e.value.code == 2 and "--file is required" in capsys.readouterr().err
    with pytest.raises(SystemExit) as e:
        vectors.main(["--check", "--file", str(tmp_path / "nope.json")])
    assert e.value.code == 2 and "no such file" in capsys.readouterr().err


def test_flash_cli_requires_image_and_backup_dir_outside_a_checkout(monkeypatch, capsys, tmp_path):
    monkeypatch.setattr(flash, "DEFAULT_BACKUP_DIR", None)
    with pytest.raises(SystemExit) as e:
        flash.main(["--image", str(tmp_path / "x.bin"), "--persist-only"])
    assert e.value.code == 2 and "--backup-dir is required" in capsys.readouterr().err
    monkeypatch.setattr(flash, "DEFAULT_IMAGE", None)
    with pytest.raises(SystemExit) as e:
        flash.main(["--no-fel"])
    assert e.value.code == 2 and "--image is required" in capsys.readouterr().err
    f = flash.Flasher(image=tmp_path / "x.bin", persist_only=True, out=lambda s: None)
    with pytest.raises(flash.FlashError, match="--backup-dir"):
        f.persist_image()


def test_sd_write_requires_a_backup_dir_outside_a_checkout(monkeypatch, tmp_path):
    monkeypatch.setattr(sdcli, "DEFAULT_BACKUP_DIR", None)
    f = tmp_path / "img.bin"
    f.write_bytes(b"\0" * 512)
    out: list[str] = []

    def no_scope(url):
        raise AssertionError("must fail before connecting")
    assert sdcli.run(["--port", "COM999", "write", str(f), "--sector", "1000", "--yes"], scope_factory=no_scope,
                     out=out.append) == 2
    assert "--backup-dir" in out[-1]


def test_bench_requires_bench_link_outside_a_checkout():
    with pytest.raises(SystemExit, match="--bench-link"):
        bench.load_bench_link(None)
    with pytest.raises(SystemExit, match="not found"):
        bench.load_bench_link(Path("does/not/exist.py"))
