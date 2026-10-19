#!/usr/bin/env python3
"""Reproducible screenshots of the scope for the documentation site.

Walks a list of named states. Each state puts the scope into a known condition through the host library
(``fnirsi1013d.scope.Scope``), reads the 800x480 screen with READ_SCREEN (protocol 8.7, the same call as the MCP tool
``scope_screenshot``) and writes ``docs/screenshots/<name>.png``. The pages under docs/ refer to a picture as
``![caption](screenshot:<name>)`` and show a "Screenshot pending" note until the file exists (tools/mkdocs_hooks.py).

    python tools/screenshots.py --list
    python tools/screenshots.py --port auto                      # every state the host can set up
    python tools/screenshots.py --port COM10 --only scope --only spectrum
    python tools/screenshots.py --simulator                      # no hardware: the simulator's synthetic screen

What the host CAN set: channel range / enable / coupling, time base, trigger, the generator (opt-in, see below),
RUN / STOP, and the display mode only for the Frequency response (BODE_CTRL open / close). What it CANNOT: open a
menu, the Spectrum analyser or phosphor view, the Settings screen, Diagnostics, the calibration wizard, the picture
and waveform viewers or the USB drive screen. Those are "manual" states: put the scope into the state by hand, then
run ``--only <name>``; the screen is captured as it is. Manual states are skipped in a run without ``--only``.
(A menu that is open makes most commands answer E_BUSY; READ_SCREEN is expected to work while one is open, check this
on the unit once the firmware handler, fw-screen, has landed.)

The generator drives the calibration pin. The ``--generator`` flag is needed for states that enable it (1 kHz,
50 %, with a probe tip on the calibration post as the documentation shows); without it the states leave the generator
as it is. The script opens the serial port: do not run it while another client (the MCP server, the coordinating
session) holds the port.
"""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

ROOT = Path(__file__).resolve().parent.parent

try:
    import fnirsi1013d  # noqa: F401
except ImportError:  # a checkout without an installed package
    sys.path.insert(0, str(ROOT / "mcp" / "src"))

from fnirsi1013d import protocol as P  # noqa: E402
from fnirsi1013d.scope import Scope  # noqa: E402
from fnirsi1013d.server import _png_from_rgb565  # noqa: E402

DEFAULT_OUT = ROOT / "docs" / "screenshots"
#: V/div range indices of set_channel(range_index=): 0..6 = 5 V .. 50 mV
RANGE_1V = 2


@dataclass
class Ctx:
    scope: Scope
    generator: bool  # --generator: states may enable the generator
    simulator: bool


@dataclass
class State:
    name: str
    help: str
    setup: Callable[[Ctx], None] | None  # None = manual: capture the screen as it is
    cleanup: Callable[[Ctx], None] | None = None


def _close_bode(ctx: Ctx) -> None:
    """Leave the Frequency response view if it shows (the only display mode the protocol can change)."""
    if ctx.scope.status().display_mode == P.DisplayMode.BODE:
        ctx.scope.bode_ctrl(P.BODE_CLOSE)


def _scope_view(ctx: Ctx) -> None:
    """Both channels on at 1 V/div DC, 200 us/div, rising edge trigger on CH1, RUN; optionally 1 kHz on the post."""
    s = ctx.scope
    _close_bode(ctx)
    for ch in ("CH1", "CH2"):
        s.set_channel(ch, enable=True, range_index=RANGE_1V, coupling=0)
    s.set_timebase(ui_index=P.TIME_PER_DIV.index(200e-6))
    s.set_trigger(source=0, edge=0, level=128)
    if ctx.generator:
        s.set_gen(enable=True, freq_hz=1000.0, duty=0.5)
    s.set_acq_state(True)
    time.sleep(0.5)  # a few captures so the traces and the measurement cards are drawn


def _frequency_response(ctx: Ctx) -> None:
    """The view as it opens: instruction line, empty plot. Nothing is driven (no sweep started)."""
    ctx.scope.bode_ctrl(P.BODE_OPEN)
    time.sleep(0.3)


def _frequency_response_sweep(ctx: Ctx) -> None:
    """One finished sweep, 100 Hz to 100 kHz, 40 points. Needs a device under test wired as the screen says, and
    --generator (the sweep drives the calibration post). The simulator models an RC low-pass."""
    if not ctx.generator:
        raise SystemExit("frequency-response-sweep drives the calibration post: add --generator, with a passive "
                         "device wired as docs/guide/screens.md says")
    s = ctx.scope
    s.bode_ctrl(P.BodeCtrlReq.make(P.BODE_RUN, 100.0, 100e3, points=40))
    deadline = time.monotonic() + 120
    while time.monotonic() < deadline:
        if s.bode_ctrl(P.BODE_STATUS).hdr.state in (2, 3, 4):  # done, stopped, error
            break
        time.sleep(0.5)
    else:
        raise TimeoutError("the sweep did not finish within 120 s")
    time.sleep(0.3)


def _generator_off(ctx: Ctx) -> None:
    if ctx.generator:
        ctx.scope.set_gen(enable=False)


STATES: list[State] = [
    State("scope", "Scope view: both channels, measurement cards, the MCP chip (this client is the host)",
          _scope_view, _generator_off),
    State("scope-mcp", "The same view, named for the Agent setup page", _scope_view, _generator_off),
    State("frequency-response", "Frequency response, just opened, before the first sweep",
          _frequency_response, _close_bode),
    State("frequency-response-sweep", "Frequency response after a sweep (needs a device under test, --generator)",
          lambda ctx: (_frequency_response(ctx), _frequency_response_sweep(ctx)), lambda ctx: (_close_bode(ctx),
                                                                                              _generator_off(ctx))),
    # manual: the host cannot reach these screens
    State("spectrum", "manual: Menu > Spectrum analyser with the calibration square wave on CH1", None),
    State("saved-pictures", "manual: Menu > Saved pictures", None),
    State("saved-waveforms", "manual: Menu > Saved waveforms", None),
    State("settings", "manual: Menu > Settings > Other settings", None),
    State("diagnostics", "manual: Menu > Diagnostics, Status page", None),
    State("usb-drive", "manual: Menu > USB drive (PC control is disconnected while it shows)", None),
    State("generator", "manual: the generator menu open (top bar generator button)", None),
    State("first-start", "manual: the First start screen (boot an uncalibrated unit)", None),
    State("calibration-zero", "manual: Diagnostics > Calibrate, screen 1 (Zero level)", None),
]
BY_NAME = {s.name: s for s in STATES}


def capture(scope: Scope, out: Path, name: str) -> Path:
    png = _png_from_rgb565(scope.read_screen())
    out.mkdir(parents=True, exist_ok=True)
    path = out / f"{name}.png"
    path.write_bytes(png)
    return path


def run(scope: Scope, names: list[str], out: Path, *, generator: bool, simulator: bool) -> list[Path]:
    ctx = Ctx(scope, generator, simulator)
    written: list[Path] = []
    for name in names:
        st = BY_NAME[name]
        try:
            if st.setup is not None:
                st.setup(ctx)
            else:
                print(f"{name}: manual state, capturing the screen as it is")
            path = capture(scope, out, name)
        finally:
            if st.cleanup is not None:
                st.cleanup(ctx)
        print(f"{name}: {path}")
        written.append(path)
    return written


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0], formatter_class=argparse.RawTextHelpFormatter,
                                 epilog="See the module docstring for what the host can and cannot set up.")
    ap.add_argument("--port", default="auto", help="serial port, 'auto', or a tcp://host:port URL (default auto)")
    ap.add_argument("--simulator", action="store_true",
                    help="start the in-process simulator and take the screenshots from it (synthetic screen)")
    ap.add_argument("--only", action="append", metavar="NAME", help="capture only this state (repeatable); manual "
                    "states are captured as the screen shows them")
    ap.add_argument("--list", action="store_true", help="list the states and exit")
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT, help=f"output directory (default {DEFAULT_OUT})")
    ap.add_argument("--generator", action="store_true",
                    help="allow states to enable the generator (drives the calibration pin)")
    args = ap.parse_args(argv)

    if args.list:
        for st in STATES:
            print(f"{st.name:26s} {'manual' if st.setup is None else 'auto  '}  {st.help}")
        return 0
    names = args.only or [s.name for s in STATES if s.setup is not None]
    unknown = [n for n in names if n not in BY_NAME]
    if unknown:
        ap.error(f"unknown state(s) {', '.join(unknown)}; --list shows them")

    server = None
    url = args.port
    if args.simulator:
        from fnirsi1013d.simulator import SimConfig, start_simulator
        server = start_simulator(SimConfig(variant="al3e", time_scale=0.0, ui_capture_period_s=0.0, seed=1))
        url = server.url
    try:
        with Scope.connect(url) as scope:
            run(scope, names, args.out, generator=args.generator or args.simulator, simulator=args.simulator)
    finally:
        if server is not None:
            server.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())
