"""Install the fnirsi1013d wheel into a fresh venv outside the repository and prove the MCP server works from it.

    python .github/scripts/wheel_smoke.py                    # build the wheel from mcp/ (needs `build`), then test
    python .github/scripts/wheel_smoke.py --wheel dist/fnirsi1013d-0.1.0-py3-none-any.whl
    python .github/scripts/wheel_smoke.py --keep             # leave the temporary directory for inspection

Steps, all with the working directory in a temporary directory and FNIRSI_MCP_HOME pointing into it:
1. ``python -m venv``, ``pip install <wheel>`` (dependencies from the index);
2. the package imports from the venv, not from the checkout;
3. ``fnirsi1013d-mcp --list-tools`` exits 0, lists scope_status and reports the data dir under FNIRSI_MCP_HOME;
4. an MCP stdio session with ``fnirsi1013d-mcp --sim ...`` (what Claude Code runs): initialize, list the tools,
   scope_status, scope_set_timebase (audited) and scope_capture, whose .npz must land in <home>/data and whose
   audit line in <home>/logs/audit.jsonl; nothing may be written inside the venv;
5. the checkout-only inputs fail with their clear message: ``python -m fnirsi1013d.vectors --check`` without
   --file, ``fnirsi1013d-bench`` without --bench-link.
Exit 0 when every step passed. No scope or serial port is touched.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]

CLIENT = r'''
import json, os, sys
import anyio
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client

exe, env = sys.argv[1], dict(os.environ)

async def main():
    params = StdioServerParameters(command=exe, args=["--sim", "--variant al3 --signal sine:freq=10kHz,amp=0.3"],
                                   env=env)
    with anyio.fail_after(120):
        async with stdio_client(params) as (r, w):
            async with ClientSession(r, w) as s:
                await s.initialize()
                names = sorted(t.name for t in (await s.list_tools()).tools)
                print(f"{len(names)} tools over stdio")
                assert "scope_capture" in names and "scope_status" in names, names

                async def call(name, **args):
                    res = await s.call_tool(name, args)
                    assert not res.isError, (name, res.content)
                    return res.structuredContent

                st = await call("scope_status")
                print("scope_status: control", st.get("control"), "run_state", st.get("run_state"))
                await call("scope_set_timebase", sample_rate="10MS/s")
                cap = await call("scope_capture")
                print("capture file:", cap["file"])
                print("RESULT " + json.dumps({"file": cap["file"]}))

anyio.run(main)
'''


def run(cmd: list[str], cwd: Path, env: dict[str, str], check: bool = True) -> subprocess.CompletedProcess:
    print("$ " + " ".join(cmd), flush=True)
    p = subprocess.run(cmd, cwd=cwd, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if p.stdout.strip():
        print(p.stdout.rstrip())
    if p.stderr.strip():
        print(p.stderr.rstrip(), file=sys.stderr)
    if check and p.returncode != 0:
        raise SystemExit(f"FAILED: exit {p.returncode}: {' '.join(cmd)}")
    return p


def fail(msg: str) -> None:
    raise SystemExit(f"FAILED: {msg}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--wheel", type=Path, help="wheel to test (default: build one from mcp/ with python -m build)")
    ap.add_argument("--keep", action="store_true", help="keep the temporary directory")
    a = ap.parse_args()

    tmp = Path(tempfile.mkdtemp(prefix="fnirsi1013d-wheel-")).resolve()
    if tmp.is_relative_to(REPO):
        fail(f"temporary directory {tmp} is inside the repository")
    env = {k: v for k, v in os.environ.items() if not k.startswith("FNIRSI_MCP_") and k not in ("PYTHONPATH",
                                                                                              "VIRTUAL_ENV")}
    home = tmp / "home"
    env["FNIRSI_MCP_HOME"] = str(home)
    try:
        wheel = a.wheel.resolve() if a.wheel else None
        if wheel is None:
            run([sys.executable, "-m", "build", "--wheel", "--outdir", str(tmp / "dist"), str(REPO / "mcp")], tmp, env)
            wheels = sorted((tmp / "dist").glob("fnirsi1013d-*.whl"))
            if len(wheels) != 1:
                fail(f"expected one wheel, got {wheels}")
            wheel = wheels[0]
        if not wheel.is_file():
            fail(f"{wheel}: no such wheel")

        venv = tmp / "venv"
        run([sys.executable, "-m", "venv", str(venv)], tmp, env)
        bindir = venv / ("Scripts" if os.name == "nt" else "bin")
        py = bindir / ("python.exe" if os.name == "nt" else "python")
        exe = bindir / ("fnirsi1013d-mcp.exe" if os.name == "nt" else "fnirsi1013d-mcp")
        run([str(py), "-m", "pip", "install", "--disable-pip-version-check", "-q", str(wheel)], tmp, env)

        where = run([str(py), "-I", "-c", "import fnirsi1013d.paths as p; print(p.__file__); print(p.checkout())"],
                    tmp, env).stdout.split()
        if not Path(where[0]).resolve().is_relative_to(venv) or where[1] != "None":
            fail(f"the package does not run from the venv as an installed wheel: {where}")

        out = run([str(exe), "--list-tools"], tmp, env).stdout
        if "scope_status" not in out or f"data dir:  {home / 'data'}" not in out:
            fail("--list-tools output lacks scope_status or the data dir under FNIRSI_MCP_HOME")

        before = {p for p in venv.rglob("*")}
        client = tmp / "smoke_client.py"
        client.write_text(CLIENT, encoding="utf-8")
        out = run([str(py), "-I", str(client), str(exe)], tmp, env).stdout
        res = json.loads(next(line for line in out.splitlines() if line.startswith("RESULT "))[7:])
        npz = Path(res["file"]).resolve()
        if not (npz.is_file() and npz.is_relative_to(home / "data")):
            fail(f"capture file {npz} is not under {home / 'data'}")
        audit = home / "logs" / "audit.jsonl"
        if not (audit.is_file() and "scope_set_timebase" in audit.read_text(encoding="utf-8")):
            fail(f"no audit line for scope_set_timebase in {audit}")
        stray = [p for p in venv.rglob("*") if p not in before and "__pycache__" not in p.parts]
        if stray:
            fail(f"files written inside the venv: {stray}")

        p = run([str(py), "-I", "-m", "fnirsi1013d.vectors", "--check"], tmp, env, check=False)
        if p.returncode != 2 or "--file is required" not in p.stderr:
            fail("fnirsi1013d.vectors without --file did not refuse with the clear message")
        p = run([str(bindir / "fnirsi1013d-bench"), "--port", "COM999", "--census", "0"], tmp, env, check=False)
        if p.returncode == 0 or "--bench-link" not in p.stderr + p.stdout:
            fail("fnirsi1013d-bench without --bench-link did not refuse with the clear message")
        print(f"OK: {wheel.name} installs and serves outside the checkout (files in {home})")
        return 0
    finally:
        if a.keep:
            print(f"kept {tmp}")
        else:
            shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
