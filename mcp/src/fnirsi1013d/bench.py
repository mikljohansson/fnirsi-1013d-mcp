"""``fnirsi1013d-bench``: the firmware's bench link check and acceptance census from the venv, in one command.

    fnirsi1013d-bench                    # auto-detect the scope port, link checks + census of 2000 PINGs
    fnirsi1013d-bench --port COM10 --census 0     # link checks only
    fnirsi1013d-bench --census 5000 --n 500
    fnirsi1013d-bench --fft [--reps 3]   # FFT_BENCH (fw 0.2.27+): time the on-scope FFT, print the table

Runs ``firmware/scope/host_tests/bench_link.py`` (owned by the firmware agent; it uses its own minimal frame code,
no project imports, so the census measures the link and not this package's transport). LOCAL-safe: PING,
GET_INFO, GET_STATUS, TEST_PATTERN, GET_LINK_STATS, GET_BAD_FRAME, ``#VER``, ``#RESET``. Its ``--fel`` option is
not passed through (reloading needs the coordinator's ack; use ``fnirsi1013d-flash``). Exit status is
bench_link's: 0 when every check passed.

``--fft`` runs FFT_BENCH through this package's transport instead (docs/protocol.md section 14 row 44; main loop,
the UI pass waits about 0.15 s at 3 reps, E_BUSY while a scope menu is open) and prints the timing table that
``firmware/scope/host_tests/fft_bench.py`` prints. Exit 0 on a valid answer.
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


def parse_args(argv: list[str] | None) -> argparse.Namespace:
    ap = argparse.ArgumentParser(prog="fnirsi1013d-bench", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default=os.environ.get("FNIRSI_MCP_PORT", "auto"),
                    help="scope serial port (default: auto-detect 1209:0001)")
    ap.add_argument("--census", type=int, default=2000,
                    help="PING count of the no-retry census (plus 100 x 64 KiB); 0 = link checks only")
    ap.add_argument("--n", type=int, default=200, help="PING count of the latency check")
    ap.add_argument("--fft", action="store_true", help="run FFT_BENCH instead of the link checks (fw 0.2.27+)")
    ap.add_argument("--reps", type=int, default=3, choices=range(1, 17), metavar="1-16",
                    help="FFT_BENCH runs per size (default 3)")
    a = ap.parse_args(argv)
    a.port = find_port() if a.port.lower() == "auto" else a.port
    return a


def build_argv(argv: list[str] | None) -> list[str]:
    a = parse_args(argv)
    out = [a.port, "--n", str(a.n)]
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


def format_fft_bench(b, round_trip_s: float | None = None) -> list[str]:
    """The table of firmware/scope/host_tests/fft_bench.py."""
    rt = f", round trip {round_trip_s * 1000:.0f} ms" if round_trip_s is not None else ""
    lines = [f"FFT_BENCH: {len(b.entries)} entries, reps {b.reps}{rt}, CP15 control 0x{b.cp15:08x} "
             f"(MMU {b.cp15 & 1}, D-cache {(b.cp15 >> 2) & 1}, I-cache {(b.cp15 >> 12) & 1})",
             "%6s %5s %9s %10s %10s %9s %6s %8s" % ("N", "mem", "init ms", "run min", "run max", "dB ms", "peak", "dB")]
    for e in b.entries:
        lines.append("%6d %5s %9.3f %10.3f %10.3f %9.3f %6d %8.2f"
                     % (e.n, "SRAM" if e.memory else "DDR", e.init_us / 1e3, e.run_min_us / 1e3, e.run_max_us / 1e3,
                        e.db_min_us / 1e3, e.peak_bin, e.peak_cdb / 100))
    return lines


def run_fft_bench(port: str, reps: int) -> int:
    import time

    from .scope import Scope
    from .transport import DeviceError, TransportError
    try:
        s = Scope.connect(port)
    except TransportError as e:
        print(f"fnirsi1013d-bench: cannot open {port}: {e}")
        return 2
    try:
        before = s.status().uptime_ms
        t0 = time.monotonic()
        try:
            b = s.fft_bench(reps)
        except DeviceError as e:
            print(f"FFT_BENCH failed: {e}")
            return 1
        dt = time.monotonic() - t0
        for line in format_fft_bench(b, dt):
            print(line)
        note = restart_note(before, s.status().uptime_ms, dt)
        print(note, flush=True)
        return 1 if note.startswith("DEVICE RESTARTED") or not b.entries else 0
    finally:
        s.close()


def main(argv: list[str] | None = None, uptime: Callable[[str], int | None] | None = None) -> None:
    import time

    a = parse_args(argv)
    if a.fft:
        sys.exit(run_fft_bench(a.port, a.reps))
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
