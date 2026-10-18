"""``fnirsi1013d-bench``: the firmware's bench link check and acceptance census from the venv, in one command.

    fnirsi1013d-bench                    # auto-detect the scope port, link checks + census of 2000 PINGs
    fnirsi1013d-bench --port COM10 --census 0     # link checks only
    fnirsi1013d-bench --census 5000 --n 500

Runs ``firmware/scope/host_tests/bench_link.py`` (owned by the firmware agent; it uses its own minimal frame code,
no project imports, so the census measures the link and not this package's transport). LOCAL-safe: PING,
GET_INFO, GET_STATUS, TEST_PATTERN, GET_LINK_STATS, GET_BAD_FRAME, ``#VER``, ``#RESET``. Its ``--fel`` option is
not passed through (reloading needs the coordinator's ack; use ``fnirsi1013d-flash``). Exit status is
bench_link's: 0 when every check passed.
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import sys
from pathlib import Path
from typing import Callable

from .transport import PIDCODES_TEST_PIDS, PIDCODES_VID, list_candidates

REPO = Path(__file__).resolve().parents[3]
BENCH_LINK = REPO / "firmware" / "scope" / "host_tests" / "bench_link.py"


def find_port() -> str:
    for c in list_candidates():
        if c.vid == PIDCODES_VID and c.pid in PIDCODES_TEST_PIDS:
            return c.device
    raise SystemExit("fnirsi1013d-bench: no scope serial port (1209:0001) found; pass --port")


def load_bench_link(path: Path = BENCH_LINK):
    if not path.is_file():
        raise SystemExit(f"fnirsi1013d-bench: {path} not found (needs the repository checkout, not just the "
                         "installed package)")
    spec = importlib.util.spec_from_file_location("bench_link", path)
    assert spec and spec.loader
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def build_argv(argv: list[str] | None) -> list[str]:
    ap = argparse.ArgumentParser(prog="fnirsi1013d-bench", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default=os.environ.get("FNIRSI_MCP_PORT", "auto"),
                    help="scope serial port (default: auto-detect 1209:0001)")
    ap.add_argument("--census", type=int, default=2000,
                    help="PING count of the no-retry census (plus 100 x 64 KiB); 0 = link checks only")
    ap.add_argument("--n", type=int, default=200, help="PING count of the latency check")
    a = ap.parse_args(argv)
    port = find_port() if a.port.lower() == "auto" else a.port
    out = [port, "--n", str(a.n)]
    if a.census:
        out += ["--census", str(a.census)]
    return out


def device_uptime(port: str) -> int | None:
    """GET_STATUS uptime_ms through a short session of our own (before / after bench_link), or None."""
    from . import protocol as P
    from .transport import Transport, TransportError, open_link
    try:
        t = Transport(open_link(port))
    except TransportError:
        return None
    try:
        return P.StatusResp.unpack(t.request(P.Cmd.GET_STATUS).payload).uptime_ms
    except Exception:  # noqa: BLE001
        return None
    finally:
        t.close()


def restart_note(before: int | None, after: int | None, elapsed_s: float) -> str:
    if before is None or after is None:
        return "device uptime: not available"
    if after < before:
        return f"DEVICE RESTARTED (uptime went backwards: {before} -> {after} ms)"
    if after + 2000 < before + elapsed_s * 1000 * 0.5:
        return f"device uptime {before} -> {after} ms over {elapsed_s:.1f} s: suspiciously small (restart?)"
    return f"device uptime {before} -> {after} ms (no restart)"


def main(argv: list[str] | None = None, uptime: Callable[[str], int | None] | None = None) -> None:
    import time

    args = build_argv(argv)
    mod = load_bench_link()
    port = args[0]
    up = uptime or device_uptime
    before = up(port)
    t0 = time.monotonic()
    print(f"device uptime at start: {before} ms" if before is not None else "device uptime at start: n/a",
          flush=True)
    print("bench_link.py " + " ".join(args), flush=True)
    old = sys.argv
    sys.argv = [str(BENCH_LINK), *args]
    try:
        rc = mod.main()
    finally:
        sys.argv = old
    note = restart_note(before, up(port), time.monotonic() - t0)
    print(note, flush=True)
    if note.startswith("DEVICE RESTARTED") and rc == 0:
        rc = 1
    sys.exit(rc)


if __name__ == "__main__":
    main()
