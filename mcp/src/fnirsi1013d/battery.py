"""fnirsi1013d-battery: the hardware battery. Drives the real MCP server (``python -m fnirsi1013d.server``) over stdio,
exactly as an agent does, through named groups of tool calls, and judges every step against an explicit expectation.

    fnirsi1013d-battery --port COM10                    # the unit, every default group (about 4 minutes)
    fnirsi1013d-battery --port COM10 --groups acq,gen   # some groups
    fnirsi1013d-battery --port COM10 --groups loopback  # the loopback self-test (CH1 straight on the post)
    fnirsi1013d-battery --port COM10 --groups coreorder # both probes on the post: the pairing of CH1 and CH2
    fnirsi1013d-battery --sim                           # the in-process simulator (an RC on the generator)
    fnirsi1013d-battery --list                          # the groups and their steps' expectations

Bench (BENCH below): the generator's 1 kHz square on the calibration post into an RC ladder, CH1 on the ladder input,
CH2 on its second node, both probes x1 (the coreorder group then judges CH1 and skips CH2: nothing of 499 kHz reaches
the second node). The battery sets the channels to 1 V/div DC, Filter Off and the generator to 1 kHz itself; it ends with the scope in RUN at 1 MSa/s, Filter Off, the generator at 1 kHz. The scope's serial port is
exclusive: close every other client first.

Output: one PASS / FAIL / SKIP line per step on stdout, ``battery.json`` (every call: arguments, verdict, detail,
seconds, the result without inline previews) and ``battery.md`` (a summary to paste into docs/hardware-status.md) in
``--out`` (default ``<home>/battery/<date-time>/``). Exit status 0 when nothing failed, 1 on any FAIL, 2 when the
server could not be started. Lessons of the 2026-11-06 run are built in: the trigger level is set explicitly whenever
the source changes, the generator is switched on again after the EXT steps, segment and hold-off runs use short
records, a failed capture is followed by a STOP, scope_spectrum is a SKIP unless the scope shows its Spectrum view.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import json
import math
import os
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

import anyio
import numpy as np

from . import analysis as A

BENCH = ("the generator's 1 kHz square on the calibration post into an RC ladder; CH1 on the ladder input, CH2 on its "
         "second node, both 1 V/div DC")
#: --sim: an RC on the generator (dut_in / dut_out, fc about 3.7 kHz like the bench ladder's -3 dB point), EXT the pin,
#: and the two firmware behaviours the 2026-11-06 battery found modelled (GET_TIMEBASE after a geometry snap, the
#: counter's configure answer)
SIM_ARGS = "--ch1 dut_in:r=3.3k,c=10n --ch2 dut_out:r=3.3k,c=10n --ext gen --tb-raw-snap --counter-stale-config"
#: the trigger level for CH1 / CH2 on the bench: the ladder's nodes swing about -1.24 .. +1.86 V against the probe ground
LEVEL_V = 0.3
F_GEN = 1000.0
DEFAULT_GROUPS = ("session", "config", "rates", "filters", "acq", "trigger", "counter", "gen", "bode", "log", "data",
                  "coreorder", "final")
OPTIONAL_GROUPS = ("loopback",)
#: core pairing: the dividers of the 2026-11-07 fold measurement (docs/hardware-status.md 10:2x-10:31), the generator's
#: period in clocks (prime: a core's samples cover every phase at any divider) and the smallest core-A swing (5..95 %) that
#: counts as a signal to fold at 1 V/div (the post about 77 codes; a quiet input 3..6, the ladder's second node no more)
CORE_ORDER_DIVIDERS = (0, 1, 2, 3, 4, 5, 6, 7, 9, 19, 99, 999)
FOLD_PERIOD = 401
FOLD_HZ = 200e6 / FOLD_PERIOD
FOLD_MIN_SWING_CODES = 12
CAPTURE_TOOLS = {"scope_capture", "scope_averaged_capture", "scope_capture_segments", "scope_capture_and_fft",
                 "scope_decay_fit", "scope_dvm", "scope_mask_test"}


class Skip(Exception):
    """A check that cannot be judged here (the reason is the step's detail)."""


@dataclass
class Step:
    group: str
    label: str
    tool: str
    args: dict[str, Any]
    expect: str
    status: str = "PASS"
    detail: str = ""
    seconds: float = 0.0
    error: str | None = None
    result: Any = None
    images: int = 0


@dataclass
class Run:
    steps: list[Step] = field(default_factory=list)
    facts: dict[str, Any] = field(default_factory=dict)

    def counts(self) -> dict[str, int]:
        out = {"PASS": 0, "FAIL": 0, "SKIP": 0}
        for s in self.steps:
            out[s.status] += 1
        return out


Check = Callable[[Any], str | None]


# ---- small helpers for the checks -----------------------------------------------------------------------------------


def get(d: Any, path: str, default: Any = None) -> Any:
    cur = d
    for part in path.split("."):
        if isinstance(cur, dict) and part in cur:
            cur = cur[part]
        elif isinstance(cur, list) and part.isdigit() and int(part) < len(cur):
            cur = cur[int(part)]
        else:
            return default
    return cur


def near(x: Any, want: float, rel: float) -> bool:
    return isinstance(x, (int, float)) and not isinstance(x, bool) and abs(x - want) <= rel * abs(want)


def fmt(x: Any) -> str:
    if isinstance(x, float):
        return f"{x:.6g}"
    return str(x)


def _strip(v: Any) -> Any:
    """The result without inline previews (they are in the .npz files)."""
    if isinstance(v, dict):
        return {k: _strip(x) for k, x in v.items() if k not in ("preview", "bins")}
    if isinstance(v, list):
        return [_strip(x) for x in v]
    return v


# ---- the runner -----------------------------------------------------------------------------------------------------


class Battery:
    def __init__(self, session: Any, run: Run, echo: Callable[[str], None] = print, sim: bool = False):
        self.s = session
        #: on the simulator a step whose model is known to differ from the unit is a SKIP, not a FAIL (``sim_differs``)
        self.sim = sim
        self.run = run
        self.echo = echo
        self.group = ""

    async def raw(self, tool: str, args: dict[str, Any]) -> tuple[bool, Any, str | None, int]:
        """One tool call with the confirmation handshake: (tool ok, structured result, error text, images)."""
        res = await self.s.call_tool(tool, args)
        sc = _structured(res)
        if isinstance(sc, dict) and sc.get("confirmation_required") and "token" in sc:
            res = await self.s.call_tool(tool, dict(args, confirm_token=sc["token"]))
            sc = _structured(res)
        images = sum(1 for c in res.content if getattr(c, "type", "") == "image")
        if res.isError:
            txt = " ".join(getattr(c, "text", "") for c in res.content)
            return False, None, txt[:600], images
        return True, sc, None, images

    async def step(self, label: str, tool: str, args: dict[str, Any] | None = None, *, expect: str = "tool succeeds",
                   check: Check | None = None, error_check: Check | None = None, timeout_s: float = 400.0,
                   sim_differs: str | None = None, want_image: bool = False) -> Any:
        """Call ``tool`` and judge it: the tool must succeed and ``check(result)`` return None (else its text is the
        failure). ``error_check`` judges a tool error instead (None = the error was expected). A failed capture-type
        call is followed by a STOP."""
        args = {k: v for k, v in (args or {}).items() if v is not None}
        st = Step(self.group, label, tool, args, expect)
        t0 = time.monotonic()
        ok, res, err = False, None, None
        try:
            with anyio.fail_after(timeout_s):
                ok, res, err, st.images = await self.raw(tool, args)
        except TimeoutError:
            err = f"no answer within {timeout_s:g} s"
        except Exception as e:  # noqa: BLE001 - a broken call is a FAIL, the battery goes on
            err = f"{type(e).__name__}: {e}"[:600]
        st.seconds = round(time.monotonic() - t0, 3)
        st.result, st.error = _strip(res), err
        try:
            if not ok:
                if error_check is None:
                    st.status, st.detail = "FAIL", f"tool error: {err}"
                else:
                    why = error_check(err or "")
                    st.status, st.detail = ("PASS", f"refused as expected: {(err or '')[:160]}") if why is None \
                        else ("FAIL", why)
            elif error_check is not None and check is None:
                st.status, st.detail = "FAIL", "expected a refusal, the tool succeeded"
            else:
                why = check(res) if check is not None else None
                if why is None and want_image and not st.images:
                    why = "no image in the answer"
                st.status = "PASS" if why is None else "FAIL"
                if why is not None and self.sim and sim_differs:
                    st.status, why = "SKIP", f"simulator differs from the unit ({sim_differs}): {why}"
                st.detail = (why or "") if why is not None else _ok_detail(res)
        except Skip as e:
            st.status, st.detail = "SKIP", str(e)
        except Exception as e:  # noqa: BLE001 - a check that crashed on an odd result fails the step
            st.status, st.detail = "FAIL", f"check raised {type(e).__name__}: {e}"
        self.run.steps.append(st)
        self.echo(f"{st.status:4s} [{label}] {tool} ({st.seconds:.2f} s): {st.detail}")
        failed_capture = tool in CAPTURE_TOOLS and (not ok or (isinstance(res, dict) and res.get("ok") is False))
        if failed_capture:
            try:
                await self.raw("scope_acquisition_control", {"action": "stop"})
            except Exception:  # noqa: BLE001
                pass
        return res if ok else None

    def note(self, label: str, status: str, expect: str, detail: str) -> None:
        """A judged step that is not a tool call (a comparison of earlier results)."""
        st = Step(self.group, label, "-", {}, expect, status, detail)
        self.run.steps.append(st)
        self.echo(f"{st.status:4s} [{label}] {expect}: {detail}")


def _structured(res: Any) -> Any:
    sc = res.structuredContent
    if sc is None:
        txt = [c.text for c in res.content if getattr(c, "type", "") == "text"]
        try:
            sc = json.loads(txt[0]) if txt else {}
        except (ValueError, IndexError):
            sc = {"text": txt}
    if isinstance(sc, dict) and set(sc) == {"result"}:
        sc = sc["result"]
    return sc


def _ok_detail(res: Any) -> str:
    if isinstance(res, dict) and "caveats" in res and res["caveats"]:
        return f"ok ({len(res['caveats'])} caveats)"
    return "ok"


# ---- checks -----------------------------------------------------------------------------------------------------------


def all_of(*checks: Check) -> Check:
    def run(res: Any) -> str | None:
        notes = []
        for c in checks:
            why = c(res)
            if why is not None:
                return why
        return "; ".join(notes) or None
    return run


def fired(n: int = 3000, rate: float | None = None) -> Check:
    def c(res: Any) -> str | None:
        if get(res, "trigger") != "fired":
            return f"trigger {get(res, 'trigger')!r}, expected 'fired'"
        if get(res, "n_samples") != n:
            return f"{get(res, 'n_samples')} samples, expected {n}"
        if rate is not None and not near(get(res, "sample_rate"), rate, 1e-6):
            return f"sample_rate {fmt(get(res, 'sample_rate'))}, expected {fmt(rate)}"
        return None
    return c


def untriggered(res: Any) -> str | None:
    if get(res, "ok") is False and get(res, "trigger") == "none":
        return None
    return f"expected no trigger, got trigger {get(res, 'trigger')!r}"


def ch_freq(ch: str, f: float, rel: float) -> Check:
    def c(res: Any) -> str | None:
        got = get(res, f"channels.{ch}.frequency")
        if not near(got, f, rel):
            return f"{ch} frequency {fmt(got)} (reason {get(res, f'channels.{ch}.frequency_reason')!r}), expected " \
                   f"{fmt(f)} +- {rel:.1%}"
        return None
    return c


def ch_no_freq(ch: str) -> Check:
    def c(res: Any) -> str | None:
        got = get(res, f"channels.{ch}.frequency")
        if got is not None:
            return f"{ch} frequency {fmt(got)} on a record without a whole period, expected none (finding 4a)"
        return None
    return c


def ch_vpp(ch: str, lo: float, hi: float) -> Check:
    def c(res: Any) -> str | None:
        got = get(res, f"channels.{ch}.vpp")
        if not isinstance(got, (int, float)) or not lo <= got <= hi:
            return f"{ch} vpp {fmt(got)}, expected {lo:g} .. {hi:g} V"
        return None
    return c


def equals(path: str, want: Any) -> Check:
    def c(res: Any) -> str | None:
        got = get(res, path)
        return None if got == want else f"{path} = {fmt(got)}, expected {fmt(want)}"
    return c


def approx(path: str, want: float, rel: float) -> Check:
    def c(res: Any) -> str | None:
        got = get(res, path)
        return None if near(got, want, rel) else f"{path} = {fmt(got)}, expected {fmt(want)} +- {rel:.2%}"
    return c


def ok_true(res: Any) -> str | None:
    return None if get(res, "ok") is True else f"ok = {get(res, 'ok')!r}"


# ---- the groups -----------------------------------------------------------------------------------------------------


async def g_session(b: Battery) -> None:
    def info(res: Any) -> str | None:
        b.run.facts["info"] = {k: get(res, k) for k in ("fw_version", "fw_build", "protocol", "fpga_version", "serial",
                                                       "link")}
        b.run.facts["caps"] = get(res, "caps", [])
        if get(res, "fpga_mismatch"):
            return f"FPGA {get(res, 'fpga_version')} is not the one the firmware was built for " \
                   f"({get(res, 'fw_fpga_version')})"
        return None

    await b.step("S1", "scope_info", expect="answers, no FPGA mismatch", check=info)
    await b.step("S2", "scope_profile")
    await b.step("S3", "scope_status", check=ok_true)
    await b.step("S4", "scope_health", check=ok_true)
    await b.step("S5", "scope_get_config", check=ok_true)
    await b.step("S6", "scope_read_calibration", check=ok_true)
    await b.step("S7", "scope_calibration_info", check=ok_true)
    await b.step("S8", "scope_event_log", {"last": 32}, check=ok_true)

    def sd(res: Any) -> str | None:
        bad = [h for h in get(res, "headers", []) if h.get("check") != "ok"]
        return None if not bad else f"headers not ok: {bad}"
    await b.step("S9", "scope_sd_info", {"verify": True}, expect="every image header checks", check=sd)
    await b.step("S10", "scope_screenshot", expect="an image", want_image=True)


async def g_config(b: Battery) -> None:
    await b.step("C0", "scope_set_decimation", {"filter": "off"}, check=equals("applied.effective", "off"))
    for i, ch in enumerate(("CH1", "CH2")):
        await b.step(f"C{i + 1}", "scope_set_channel", {"channel": ch, "volts_per_div": 1.0, "coupling": "DC"},
                     expect="1 V/div DC", check=equals("applied.volts_per_div", 1.0))
    await gen_1k(b, "C3")
    await trig_ch1(b, "C4")


async def gen_1k(b: Battery, label: str) -> None:
    await b.step(label, "scope_generator", {"enabled": True, "frequency": "1kHz", "duty_percent": 50},
                 expect="the generator at 1 kHz 50 %", check=approx("applied.frequency_exact_hz", F_GEN, 1e-9))


async def trig_ch1(b: Battery, label: str, **kw: Any) -> None:
    args = dict(source="CH1", type="edge", edge="rising", level=LEVEL_V, mode="normal", hold_off=0,
                pretrigger_fraction=0.5, total_samples=3000)
    args.update(kw)
    await b.step(label, "scope_set_trigger", args, expect=f"{args['source']} {args['edge']} {args['mode']}, level "
                 "set explicitly", check=ok_true)


async def g_rates(b: Battery) -> None:
    await b.step("R0", "scope_set_decimation", {"filter": "off"}, check=equals("applied.effective", "off"))
    for i, (name, rate) in enumerate((("200MS/s", 200e6), ("20MS/s", 20e6), ("2MS/s", 2e6), ("200kS/s", 200e3),
                                      ("20kS/s", 20e3))):
        await b.step(f"R{i}a", "scope_set_timebase", {"sample_rate": name},
                     check=approx("applied.sample_rate", rate, 1e-9))
        periods = 3000 / rate * F_GEN
        checks: list[Check] = [fired(3000, rate)]
        if periods >= 3:
            exp = f"triggered, 3000 samples at {name}, CH1 1 kHz +- 0.5 %, CH1 vpp 2.6 .. 3.7 V"
            checks += [ch_freq("CH1", F_GEN, 0.005), ch_vpp("CH1", 2.6, 3.7)]
        elif periods < 1:
            exp = f"triggered, 3000 samples at {name}; no frequency on either channel (no whole period)"
            checks += [ch_no_freq("CH1"), ch_no_freq("CH2")]
        else:
            exp = f"triggered, 3000 samples at {name}"
        await b.step(f"R{i}b", "scope_capture", {"channels": ["CH1", "CH2"], "timeout_s": 3.0}, expect=exp,
                     check=all_of(*checks))
        await b.step(f"R{i}c", "scope_measure", {"items": ["vpp", "mean", "frequency", "duty", "rise_time", "delay"],
                                                 "channels": ["CH1", "CH2"]}, check=ok_true)


async def g_filters(b: Battery) -> None:
    await b.step("F0", "scope_set_timebase", {"sample_rate": "1MS/s"}, check=approx("applied.sample_rate", 1e6, 1e-9))
    for i, f in enumerate(("off", "boxcar", "boxcar+fir", "cic2", "cic2+fir", "cic3", "cic3+fir", "peak")):
        await b.step(f"F{i}a", "scope_set_decimation", {"filter": f}, expect=f"Filter {f} in effect",
                     check=equals("applied.effective", f))

        def vpp_vs_off(res: Any, f: str = f) -> str | None:
            v = get(res, "channels.CH1.vpp")
            if f == "off":
                b.run.facts["vpp_off"] = v
                return None
            ref = b.run.facts.get("vpp_off")
            if ref and not near(v, ref, 0.08):
                return (f"CH1 vpp {fmt(v)} against {fmt(ref)} with Filter Off (expected within 8 %: Filter Off's "
                        "vpp holds the noise peaks)")
            return None
        await b.step(f"F{i}b", "scope_capture", {"channels": ["CH1", "CH2"], "timeout_s": 3.0,
                                                 "peak": True if f == "peak" else None},
                     expect="triggered, CH1 1 kHz +- 0.1 %, vpp within 8 % of Filter Off",
                     check=all_of(fired(3000, 1e6), ch_freq("CH1", F_GEN, 0.001), vpp_vs_off))
        await b.step(f"F{i}c", "scope_measure", {"items": ["vpp", "mean", "frequency", "duty"],
                                                 "channels": ["CH1", "CH2"]},
                     expect="CH1 duty 0.5 +- 0.02",
                     check=lambda r: None if near(get(r, "channels.CH1.duty"), 0.5, 0.04) else
                     f"CH1 duty {fmt(get(r, 'channels.CH1.duty'))}")
    await b.step("F9", "scope_set_decimation", {"filter": "off"}, check=equals("applied.effective", "off"))


async def g_acq(b: Battery) -> None:
    both = ["CH1", "CH2"]
    await b.step("A0", "scope_set_timebase", {"sample_rate": "200kS/s"},
                 check=approx("applied.sample_rate", 200e3, 1e-9))
    await trig_ch1(b, "A0t")
    avg = all_of(equals("n_used", 16), approx("dt", 5e-6, 1e-6))
    await b.step("A1", "scope_averaged_capture", {"n": 16, "channels": both}, expect="16 of 16 averaged at 5 us",
                 check=avg)
    await b.step("A2", "scope_averaged_capture", {"n": 16, "channels": both, "on_device": True},
                 expect="16 of 16 averaged", check=avg)
    await b.step("A3", "scope_averaged_capture", {"n": 16, "channels": both, "hardware": True},
                 expect="16 averaged in the FPGA", check=all_of(avg, equals("averaging", "hardware")))
    await b.step("A4", "scope_averaged_capture", {"n": 16, "channels": both, "align": "edge", "align_channel": "CH1"},
                 expect="16 of 16 edge-aligned", check=all_of(avg, equals("align", "edge")))
    # segments need a record short enough for the count; the record change is the step that snaps the UI's time/div
    # (finding 3), so the FFT and DVM below run where the 2026-11-06 runs mislabelled the rate
    await trig_ch1(b, "A5a", total_samples=1000)

    def seg_interval(res: Any, record_s: float = 1000 * 5e-6) -> str | None:
        iv = get(res, "interval_s.mean")
        if not isinstance(iv, (int, float)) or iv < record_s or abs(iv * F_GEN - round(iv * F_GEN)) > 0.01:
            return f"segment interval {fmt(iv)} s, expected a whole number of 1 ms periods >= {record_s:g} s"
        return None
    await b.step("A5", "scope_capture_segments", {"count": 8, "channels": both},
                 expect="8 of 8 segments triggered, interval a whole number of periods",
                 check=all_of(equals("completed", 8), equals("untriggered", 0), seg_interval))
    await trig_ch1(b, "A5b")

    def fft(res: Any) -> str | None:
        fs = get(res, "sample_rate")
        if not near(fs, 200e3, 1e-6):
            return f"sample_rate {fmt(fs)}, expected 200000 (finding 3: the rate the FPGA holds)"
        pk = get(res, "peaks.0.frequency")
        res_hz = get(res, "frequency_resolution") or 0
        if not isinstance(pk, (int, float)) or abs(pk - F_GEN) > max(res_hz, 1.0):
            return f"strongest peak at {fmt(pk)} Hz, expected 1000 Hz within one bin ({fmt(res_hz)} Hz)"
        return None
    await b.step("A6", "scope_capture_and_fft", {"channel": "CH1", "n_avg": 4, "peaks": 6},
                 expect="200 kSa/s, the 1 kHz fundamental first", check=fft)
    await b.step("A7", "scope_capture_and_fft", {"channel": "CH2", "n_avg": 4, "peaks": 6, "window_fn": "flattop"},
                 expect="200 kSa/s, the 1 kHz fundamental first", check=fft)

    def dvm(res: Any) -> str | None:
        f = get(res, "channels.CH1.frequency")
        if not near(f, F_GEN, 0.01):
            return f"CH1 frequency {fmt(f)}, expected 1000 +- 1 % (finding 3)"
        if not any("over 15 ms" in c for c in get(res, "caveats", [])):
            return "the record-length caveat does not say 3000 samples over 15 ms"
        return None
    await b.step("A8", "scope_dvm", {"channels": both, "n": 8}, expect="1 kHz, records of 15 ms", check=dvm)
    await b.step("A9", "scope_fetch_waveform", {"channels": both}, expect="the last capture at 200 kSa/s",
                 check=approx("sample_rate", 200e3, 1e-6))
    await b.step("A10", "scope_acquisition_control", {"action": "arm"}, expect="ARMED (or already TRIGGERED)",
                 check=lambda r: None if get(r, "acq_state") in ("ARMED", "TRIGGERED", "DONE") else
                 f"acq_state {get(r, 'acq_state')}")
    await b.step("A11", "scope_acquisition_control", {"action": "wait", "timeout_s": 3.0},
                 check=equals("acq_state", "DONE"))
    await b.step("A12", "scope_acquisition_control", {"action": "stop"}, check=ok_true)
    await b.step("A13", "scope_capture", {"channels": both, "raw": True, "timeout_s": 3.0},
                 check=all_of(fired(3000, 200e3), ch_freq("CH1", F_GEN, 0.005)))
    await b.step("A14", "scope_capture", {"channels": both, "processed": True, "timeout_s": 3.0},
                 check=fired(3000, 200e3))
    await b.step("A15", "scope_set_timebase", {"sample_rate": "2MS/s"}, check=approx("applied.sample_rate", 2e6, 1e-9))
    await trig_ch1(b, "A15t", edge="falling", pretrigger_fraction=0.1)

    def decay(res: Any) -> str | None:
        tau, r2 = get(res, "tau"), get(res, "fit.r2")
        if not isinstance(tau, (int, float)) or not 10e-6 <= tau <= 1e-3:
            return f"tau {fmt(tau)} s, expected 10 us .. 1 ms (bench 74.8 us)"
        if not isinstance(r2, (int, float)) or r2 < 0.99:
            return f"r2 {fmt(r2)}, expected >= 0.99"
        return None
    await b.step("A16", "scope_decay_fit", {"channel": "CH2", "edge_channel": "CH1", "edge": "falling",
                                            "t_start": "20us", "t_end": "400us", "model": "exp+offset", "n_avg": 16},
                 expect="CH2's decay after CH1's falling edge, r2 >= 0.99", check=decay)
    await trig_ch1(b, "A16t")

    def spectrum_refused(err: str) -> str | None:
        if "scope view" in err or "Spectrum analyzer" in err:
            raise Skip("the scope shows its scope view: scope_spectrum needs main menu > Spectrum analyzer")
        return f"refused for another reason: {err[:200]}"
    await b.step("A17", "scope_spectrum", {"channel": "CH1"}, expect="the on-scope spectrum (Spectrum view only)",
                 check=ok_true, error_check=spectrum_refused)


async def g_trigger(b: Battery) -> None:
    both = ["CH1", "CH2"]
    cap = {"channels": both, "timeout_s": 3.0}
    none = {"channels": both, "timeout_s": 2.0}
    ok = all_of(fired(3000, 200e3), ch_freq("CH1", F_GEN, 0.005))
    await b.step("T0", "scope_set_timebase", {"sample_rate": "200kS/s"},
                 check=approx("applied.sample_rate", 200e3, 1e-9))
    await b.step("T0d", "scope_set_decimation", {"filter": "off"}, check=equals("applied.effective", "off"))
    await trig_ch1(b, "T1a", edge="falling")
    await b.step("T1b", "scope_capture", cap, expect="fires on CH1's falling edge", check=ok)
    # the source changes: the level is set with it (CH2's stored level was code 250 on the unit)
    await trig_ch1(b, "T2a", source="CH2")
    await b.step("T2b", "scope_capture", cap, expect="fires on CH2's rising edge", check=ok)
    await trig_ch1(b, "T3a", source="CH2", edge="falling")
    await b.step("T3b", "scope_capture", cap, expect="fires on CH2's falling edge", check=ok)
    await trig_ch1(b, "T4a")
    typed = [
        ("T5", dict(type="pulse_width", polarity="positive", width="400us", longer=True), True,
         "a 500 us high pulse is longer than 400 us: fires"),
        ("T6", dict(type="pulse_width", polarity="positive", width="700us", longer=True), False,
         "no pulse is longer than 700 us: no trigger"),
        ("T7", dict(type="pulse_width", polarity="positive", width="700us", longer=False), True,
         "500 us is shorter than 700 us: fires"),
        ("T8", dict(type="pulse_width", polarity="negative", width="300us", longer=False), False,
         "no low pulse is shorter than 300 us: no trigger"),
        ("T9", dict(type="nth_edge", polarity="rising", n=3, idle=0), True, "the 3rd rising edge: fires"),
        ("T10", dict(type="nth_edge", polarity="rising", n=3, idle="5ms"), False,
         "the square never idles 5 ms: no trigger"),
        ("T11", dict(type="timeout", polarity="high", width="300us"), True, "high for 300 us: fires"),
        ("T12", dict(type="timeout", polarity="high", width="800us"), False, "never high for 800 us: no trigger"),
    ]
    for label, kw, fires, why in typed:
        await b.step(f"{label}a", "scope_set_trigger", dict(source="CH1", level=LEVEL_V, mode="normal", hold_off=0,
                                                            pretrigger_fraction=0.5, total_samples=3000, **kw),
                     check=ok_true)
        await b.step(f"{label}b", "scope_capture", cap if fires else none, expect=why,
                     check=ok if fires else untriggered)
    # hold-off: short records (80 samples = 0.4 ms) so the interval is the hold-off's, not the record's
    for label, hold, want in (("T13", 0, 1e-3), ("T14", "2.5ms", 3e-3)):
        await trig_ch1(b, f"{label}a", total_samples=80, hold_off=hold)
        await b.step(f"{label}b", "scope_capture_segments", {"count": 8, "channels": ["CH1"]},
                     expect=f"8 segments {want * 1e3:g} ms apart",
                     check=all_of(equals("completed", 8), approx("interval_s.mean", want, 0.01)))
    await trig_ch1(b, "T15a", mode="auto", pretrigger_fraction=0.1, total_samples=16000)
    await b.step("T15b", "scope_capture", cap, expect="auto mode with a signal: triggered, 16000 samples",
                 check=all_of(fired(16000, 200e3), ch_freq("CH1", F_GEN, 0.005)))

    def auto_untriggered(res: Any) -> str | None:
        if get(res, "trigger") != "auto_timeout":
            return f"trigger {get(res, 'trigger')!r}, expected 'auto_timeout'"
        return ch_vpp("CH1", 2.6, 3.7)(res)
    await b.step("T16a", "scope_set_trigger", dict(source="CH1", type="edge", edge="rising", level_code=250,
                                                   mode="auto", hold_off=0, pretrigger_fraction=0.5,
                                                   total_samples=3000), check=ok_true)
    await b.step("T16b", "scope_capture", {"channels": both, "timeout_s": 5.0},
                 expect="auto mode, level above the signal: the record ends untriggered and shows the square",
                 check=auto_untriggered)
    if "EXT_TRIGGER" in b.run.facts.get("caps", ["EXT_TRIGGER"]):
        def gen_off(res: Any) -> str | None:
            if not any("generator was switched off" in c for c in get(res, "caveats", [])):
                return "no caveat that selecting EXT switched the generator off"
            return None
        await b.step("T17a", "scope_set_trigger", dict(source="EXT", type="edge", edge="rising", mode="normal",
                                                       hold_off=0, pretrigger_fraction=0.5, total_samples=3000),
                     expect="EXT selected, the generator switched off (the pin is the input)", check=gen_off)
        await b.step("T17b", "scope_capture", none, expect="nothing on the EXT pin: no trigger", check=untriggered)
    # back to the bench signal: CH1 with its level, and the generator on again (EXT switched it off)
    await trig_ch1(b, "T18")
    await gen_1k(b, "T19")


async def g_counter(b: Battery) -> None:
    await b.step("K0", "scope_set_timebase", {"sample_rate": "200MS/s"}, check=approx("applied.sample_rate", 200e6, 1e-9))
    await gen_1k(b, "K0g")

    def counted(f: float, rel: float, duty: float | None = None) -> Check:
        def c(res: Any) -> str | None:
            rows = get(res, "readings", [])
            if not rows:
                return "no reading"
            for r in rows:  # every reading: the first after a configuration too (finding 4e)
                if not r.get("valid"):
                    return f"reading seq {r.get('seq')} not valid ({res.get('caveats')})"
                if not near(r.get("frequency_hz"), f, rel):
                    return f"reading seq {r.get('seq')}: {fmt(r.get('frequency_hz'))} Hz, expected {fmt(f)} +- {rel:g}"
                if duty is not None and not near(r.get("duty"), duty, 0.04 / duty):
                    return f"reading seq {r.get('seq')}: duty {fmt(r.get('duty'))}, expected {duty:g} +- 0.04"
            return None
        return c
    await b.step("K1", "scope_counter", {"source": "CH1", "gate": "100ms", "readings": 3},
                 expect="CH1: every reading valid, 1 kHz +- 1e-5", check=counted(F_GEN, 1e-5))
    await b.step("K2", "scope_counter", {"source": "CH1", "gate": "1s", "readings": 1},
                 expect="1 s gate: valid, 1 kHz +- 1e-6", check=counted(F_GEN, 1e-6))
    await b.step("K3", "scope_counter", {"source": "CH2", "gate": "100ms", "readings": 2},
                 expect="CH2 (the ladder's second node): valid, 1 kHz +- 1e-4", check=counted(F_GEN, 1e-4))
    await b.step("K4", "scope_counter", {"source": "EXT", "gate": "100ms", "readings": 2},
                 expect="EXT (the pin the generator drives): valid, 1 kHz +- 1e-6", check=counted(F_GEN, 1e-6))
    b.run.facts["counted"] = counted


async def g_gen(b: Battery) -> None:
    counted = b.run.facts.get("counted")
    if counted is None:  # the counter group was not run: its check factory is the same
        await g_counter_factory(b)
        counted = b.run.facts["counted"]
    await b.step("G0", "scope_set_timebase", {"sample_rate": "200MS/s"}, check=approx("applied.sample_rate", 200e6, 1e-9))
    for label, f, duty, cnt in (("G1", 5e3, 50, [("EXT", "100ms"), ("CH1", "100ms")]),
                                ("G2", 5e3, 25, [("EXT", "100ms")]),
                                ("G3", 999.5, 50, [("EXT", "1s")]),
                                ("G4", 1e6, 50, [("EXT", "100ms")])):
        await b.step(label, "scope_generator", {"enabled": True, "frequency": f, "duty_percent": duty},
                     expect=f"{f:g} Hz {duty} %", check=approx("applied.frequency_exact_hz", f, 1e-9))
        for j, (src, gate) in enumerate(cnt):
            rel = 1e-6 if src == "EXT" else 1e-5
            await b.step(f"{label}{'abc'[j]}", "scope_counter", {"source": src, "gate": gate, "readings": 2},
                         expect=f"{src}: every reading valid, {f:g} Hz, duty {duty / 100:g} (also the first after the "
                                "change)", check=counted(f, rel, duty / 100 if src == "EXT" else None))

    def sweep(res: Any) -> str | None:
        st = get(res, "stats", {})
        if st.get("steps") != 200 or not get(res, "generator_restored"):
            return f"steps {st.get('steps')}, generator restored {get(res, 'generator_restored')}"
        b.run.facts["sweep_rate"] = st.get("achieved_steps_per_s")
        return None
    r = await b.step("G5", "scope_sweep", {"start": "1kHz", "stop": "10kHz", "steps": 200, "time_s": 2.0,
                                           "spacing": "log"},
                     expect="200 steps sent, the generator restored (the pace is reported)", check=sweep)
    if r is not None:
        st = r.get("stats", {})
        b.run.steps[-1].detail = (f"ok: {fmt(st.get('achieved_steps_per_s'))} steps/s of 100 asked, "
                                  f"{st.get('pending_polls')} polls, {st.get('pending_sleeps')} sleeps, "
                                  f"{st.get('late_steps')} late")
    await b.step("G6", "scope_generator", {"enabled": False}, check=equals("applied.enabled", False))

    def no_count(res: Any) -> str | None:
        return None if get(res, "valid") is False else "a valid count with the generator off"
    await b.step("G7", "scope_counter", {"source": "EXT", "gate": "100ms", "readings": 1},
                 expect="generator off: no valid count on EXT", check=no_count)
    await gen_1k(b, "G8")
    await b.step("G9", "scope_counter", {"source": "CH1", "gate": "100ms", "readings": 2},
                 expect="CH1: valid, 1 kHz", check=counted(F_GEN, 1e-5))


async def g_counter_factory(b: Battery) -> None:
    """The counter check factory without running the counter group's steps."""
    saved, b.run.steps = b.run.steps, []
    echo, b.echo = b.echo, (lambda _s: None)
    try:
        class _Null:
            async def call_tool(self, *_a: Any, **_k: Any) -> Any:
                raise RuntimeError("not run")
        real, b.s = b.s, _Null()
        try:
            await g_counter(b)
        finally:
            b.s = real
    finally:
        b.run.steps, b.echo = saved, echo


async def g_bode(b: Battery) -> None:
    sweep = {"start": "100Hz", "stop": "200kHz", "points": 24, "background": False, "max_duration_s": 180}

    def bode(res: Any) -> str | None:
        sm = get(res, "summary", {})
        if get(res, "points") != get(res, "planned") or get(res, "stopped_early"):
            return f"{get(res, 'points')} of {get(res, 'planned')} points, stopped early: {get(res, 'stopped_early')}"
        if (sm.get("valid_points") or 0) < 20:
            return f"{sm.get('valid_points')} valid points of 24, expected >= 20"
        if sm.get("shape") != "low-pass" or not sm.get("minus_3db_hz"):
            return f"shape {sm.get('shape')!r}, -3 dB {sm.get('minus_3db_hz')}: expected a low-pass with a -3 dB point"
        return None
    host = await b.step("B1", "scope_bode_sweep", dict(sweep, where="host"), expect="host sweep: >= 20 valid points, "
                        "a low-pass with a -3 dB point", check=bode)
    dev = await b.step("B2", "scope_bode_sweep", dict(sweep, where="device", close_view=True),
                       expect="the scope's own sweep: all 24 points, >= 20 valid", check=bode)
    if host and dev:
        rows_h = {round(r["f_requested"], 3): r for r in get(host, "table", [])}
        worst_db = worst_deg = 0.0
        n = 0
        for r in get(dev, "table", []):
            h = rows_h.get(round(r["f_requested"], 3))
            if h is None or r["f_requested"] >= 40e3 or h.get("flags") or r.get("flags"):
                continue
            if h.get("mag_db") is None or r.get("mag_db") is None:
                continue
            n += 1
            worst_db = max(worst_db, abs(h["mag_db"] - r["mag_db"]))
            d = (h["phase_deg"] - r["phase_deg"] + 180) % 360 - 180
            worst_deg = max(worst_deg, abs(d))
        ok = n >= 10 and worst_db <= 0.1 and worst_deg <= 0.5
        b.note("B2c", "PASS" if ok else "FAIL", "host and device within 0.1 dB / 0.5 deg below 40 kHz",
               f"{n} points compared, worst {worst_db:.3f} dB / {worst_deg:.2f} deg")
    else:
        b.note("B2c", "SKIP", "host and device within 0.1 dB / 0.5 deg below 40 kHz", "a sweep failed")
    await b.step("B3", "scope_report", {"source": "latest", "title": "Battery: frequency response"},
                 expect="a report page", check=lambda r: None if get(r, "html") else "no html")
    await gen_1k(b, "B4")


async def g_log(b: Battery) -> None:
    def status(state: str, samples: int | None = None, rate: float | None = None) -> Check:
        def c(res: Any) -> str | None:
            if get(res, "state") != state:
                return f"state {get(res, 'state')!r}, expected {state!r} (error {get(res, 'error')})"
            if get(res, "gaps") != 0 or get(res, "lost_samples") != 0:
                return f"{get(res, 'gaps')} gaps, {get(res, 'lost_samples')} samples lost"
            got = get(res, "samples")
            if samples is not None and got != samples:
                return f"{got} samples, expected {samples}"
            if rate is not None and not near(got / max(get(res, "seconds_logged") or 1e-9, 1e-9), rate, 0.01):
                return f"{got} samples in {fmt(get(res, 'seconds_logged'))} s, expected {rate:g} /s"
            return None
        return c
    await b.step("L1", "scope_log", {"rate": "500k", "channels": ["CH1"], "bits": 8, "filter": "off", "duration": 3},
                 check=ok_true)
    await anyio.sleep(4.5)
    await b.step("L2", "scope_log_status", expect="ended, 1,500,000 samples, 0 gaps",
                 check=status("ended", 1_500_000))
    await b.step("L3", "scope_log_stop", check=ok_true)
    await b.step("L4", "scope_log", {"rate": "125k", "channels": ["CH1", "CH2"], "bits": 16, "duration": 3},
                 check=ok_true)
    await anyio.sleep(4.5)
    await b.step("L5", "scope_log_status", expect="ended, 375,000 samples per channel, 0 gaps",
                 check=status("ended", 375_000))
    await b.step("L6", "scope_log_stop", check=ok_true)
    await b.step("L7", "scope_log", {"rate": "100k", "channels": ["CH1"], "bits": 8}, check=ok_true)
    await anyio.sleep(2.0)
    await b.step("L8", "scope_log_status", expect="running, 0 gaps", check=status("running"))
    await b.step("L9", "scope_log_stop", expect="stopped, 100 kSa/s logged, 0 gaps",
                 check=status("stopped", rate=100e3))
    await b.step("L10", "scope_log_status", check=status("stopped"))


async def g_data(b: Battery) -> None:
    await b.step("D0", "scope_set_decimation", {"filter": "off"}, check=equals("applied.effective", "off"))
    await b.step("D1", "scope_set_timebase", {"sample_rate": "100kS/s"},
                 check=approx("applied.sample_rate", 100e3, 1e-9))
    # pretrigger 3.6 ms: the record starts between two edges (at 3 ms an edge sits on sample 0 and its jitter fails
    # the mask's first sample: the time tolerance cannot widen before the record)
    await trig_ch1(b, "D2", edge="falling", pretrigger_fraction=0.12)
    await b.step("D3", "scope_capture", {"channels": ["CH1", "CH2"], "timeout_s": 3.0}, check=fired(3000, 100e3))
    await b.step("D4", "scope_export_csv", {"source": "latest"}, expect="3000 rows", check=equals("rows", 3000))
    await b.step("D5", "scope_report", {"source": "latest", "title": "Battery: capture report"},
                 check=lambda r: None if get(r, "html") else "no html")

    def uart(res: Any) -> str | None:
        fr = get(res, "frames", [])
        if len(fr) < 5 or get(res, "n_errors") != 0 or any(f.get("hex") != "0x55" for f in fr):
            return f"{len(fr)} frames, {get(res, 'n_errors')} errors, values {[f.get('hex') for f in fr][:8]}"
        return None
    await b.step("D6", "scope_decode", {"protocol": "uart", "channels": ["CH1"], "baud": 2000, "max_frames": 20},
                 expect="the 1 kHz square at 2000 baud: >= 5 frames of 0x55, 0 errors", check=uart)
    for label, ch in (("D7", "CH1"), ("D8", "CH2")):
        await b.step(label, "scope_mask_test", {"channel": ch, "reference": "capture", "tolerance_div": 0.5,
                                                "tolerance_time": "100us", "n": 10},
                     expect="10 of 10 inside the mask", check=all_of(equals("all_passed", True), equals("passed", 10)))
    await b.step("D9", "scope_decode", {"protocol": "spi", "channels": ["CH1", "CH2"], "max_frames": 5},
                 expect="runs (no SPI on the bench)", check=ok_true)
    await b.step("D10", "scope_decode", {"protocol": "i2c", "channels": ["CH1", "CH2"], "max_frames": 5},
                 expect="runs (no I2C on the bench)", check=ok_true)
    await b.step("D11", "scope_save_report", expect="a report file on the card",
                 check=lambda r: None if get(r, "file") else "no file")
    await b.step("D12", "scope_screenshot", expect="an image", want_image=True)


async def g_coreorder(b: Battery, n: int = 12) -> None:
    """The pairing of the cores at every divider, 200 MSa/s included: the fold method (fpga/al3/tools/core_delay_fold.py,
    ``analysis.core_delay_fold``), the generator at 200 MHz / 401, Filter Off, raw per-core records. d = +1 expected (0x153F;
    0x153E read +3 at most rates, so this group FAILs there with the measured row). Wiring: both probes on the post is the
    sharp signal; on the bench ladder CH1 (its input) is judged on the rounded signal, the minimum shallower, and CH2 (the
    second node, nothing left of 499 kHz) is a SKIP with the reason."""
    await b.step("O0", "scope_set_decimation", {"filter": "off"}, check=equals("applied.effective", "off"))
    await b.step("O0t", "scope_set_trigger", dict(source="CH1", type="edge", edge="rising", level_code=128,
                                                  mode="auto", hold_off=0, pretrigger_fraction=0.5,
                                                  total_samples=3000), check=ok_true)
    await b.step("O0g", "scope_generator", {"enabled": True, "frequency": FOLD_HZ, "duty_percent": 50},
                 expect=f"the generator at 200 MHz / {FOLD_PERIOD}, 50 %",
                 check=approx("applied.frequency_exact_hz", FOLD_HZ, 1e-9))

    async def record(chans: list[str]) -> dict[str, tuple[np.ndarray, np.ndarray]] | None:
        """One raw record: any record serves, the fold needs no trigger."""
        ok, res, _err, _img = await b.raw("scope_capture", {"channels": chans, "raw": True, "timeout_s": 3.0})
        if not ok or not get(res, "file"):
            await b.raw("scope_acquisition_control", {"action": "stop"})
            return None
        with np.load(get(res, "file")) as z:
            if any(f"{c}_raw_a" not in z for c in chans):
                return None
            return {c: (z[f"{c}_raw_a"], z[f"{c}_raw_b"]) for c in chans}

    await b.step("O0a", "scope_set_timebase", {"divider": 0}, check=equals("applied.divider", 0))
    first = await record(["CH1", "CH2"])
    swing = {c: float(np.subtract(*np.percentile(v[0], [95, 5]))) for c, v in first.items()} if first else {}
    chans = [c for c, w in swing.items() if w >= FOLD_MIN_SWING_CODES]
    b.note("O0s", "PASS" if "CH1" in chans else "FAIL", f"CH1 carries the {FOLD_HZ / 1e3:.0f} kHz square",
           f"swing {swing.get('CH1', 0):.0f} codes (at least {FOLD_MIN_SWING_CODES}): CH1 on the post or the ladder input")
    if "CH2" not in chans:
        b.note("O0c", "SKIP", f"CH2 carries the {FOLD_HZ / 1e3:.0f} kHz square",
               f"swing {swing.get('CH2', 0):.0f} codes: CH2 not judged, on the ladder's second node nothing of it is left. "
               "Both probes on the post for both channels: fnirsi1013d-battery --port <port> --groups coreorder")
    rows: dict[str, dict[int, int]] = {c: {} for c in chans}
    if "CH1" in chans:
        for d in CORE_ORDER_DIVIDERS:
            await b.step(f"O{d}a", "scope_set_timebase", {"divider": d}, check=equals("applied.divider", d))
            recs: dict[str, list[dict[int, float]]] = {c: [] for c in chans}
            t0 = time.monotonic()
            for _ in range(n):
                got = await record(chans)
                for c, (ra, rb) in (got or {}).items():
                    recs[c].append(A.core_delay_fold(ra, rb, d, FOLD_PERIOD))
            secs = round(time.monotonic() - t0, 3)
            for c, tag in zip(chans, "bc"):
                st = Step(b.group, f"O{d}{tag}", "scope_capture", {"channels": chans, "raw": True, "captures": n},
                          f"divider {d}, {c}: the designed pairing (d = +1)", seconds=secs)
                if not recs[c]:
                    st.status, st.detail = "FAIL", "no record with raw cores"
                else:
                    best, votes, mean = A.core_delay_verdict(recs[c])
                    rows[c][d] = best
                    rms = " / ".join(f"{mean[k]:.2f}" for k in (0, 1, 2, 3))
                    st.status = "PASS" if best == 1 and votes.get(1, 0) * 2 > len(recs[c]) else "FAIL"
                    st.detail = (f"{200 / (d + 1):.4g} MSa/s: d = {best:+d} (votes {votes}, RMS d = 0 / +1 / +2 / +3 {rms})")
                    st.result = {"votes": votes, "rms": mean}
                b.run.steps.append(st)
                b.echo(f"{st.status:4s} [{st.label}] core delay ({st.seconds:.2f} s): {st.detail}")
    b.run.facts["core_order"] = rows
    text = "; ".join(f"{c} " + ", ".join(f"{d}: {v:+d}" for d, v in r.items()) for c, r in rows.items())
    fails = [x for x in b.run.steps if x.group == b.group and x.label.startswith("O") and x.status == "FAIL"]
    b.note("O", "PASS" if rows.get("CH1") and len(rows["CH1"]) == len(CORE_ORDER_DIVIDERS) and not fails else "FAIL",
           "d = +1 at every divider", f"measured row (divider: d) {text or 'none'}")
    await gen_1k(b, "Ozg")
    await b.step("Oz", "scope_set_timebase", {"sample_rate": "200kS/s"}, check=ok_true)
    await trig_ch1(b, "Ozt")


async def g_loopback(b: Battery) -> None:
    """Needs CH1 straight on the calibration post (no ladder): the self-test's levels and edges are the post's. T7 is
    a skip on a release build (FORCE is debug-only), every other check must pass, T4 and T5 among them."""
    def passed(res: Any) -> str | None:
        checks = get(res, "checks", [])
        bad = [f"{c['check']}: {c.get('reason') or c.get('measured') or c.get('measured_order') or c.get('offset_words')}"
               for c in checks if c["pass"] is False]
        if bad:
            return "; ".join(bad)[:500]
        ran = " ".join(c["check"] for c in checks if c["pass"] is True)
        if "T4" not in ran or "T5" not in ran:
            return "T4 or T5 did not run"
        return None
    await b.step("X1", "scope_self_test_loopback",
                 expect="every check passes, T4 d = +1, T5 +1 +- 1 words; T7 skipped on a release build (CH1 straight "
                        "on the post)", check=passed)
    await gen_1k(b, "X2")


async def g_final(b: Battery) -> None:
    await b.step("Z0", "scope_set_decimation", {"filter": "off"}, check=equals("applied.effective", "off"))
    await b.step("Z1", "scope_set_timebase", {"sample_rate": "1MS/s"}, check=ok_true)
    await trig_ch1(b, "Z2", mode="auto")
    await gen_1k(b, "Z2g")
    await b.step("Z3", "scope_acquisition_control", {"action": "run"}, check=equals("run_state", "RUN"))
    await b.step("Z4", "scope_health", check=ok_true)
    await b.step("Z5", "scope_event_log", {"last": 64}, check=ok_true)
    await b.step("Z6", "scope_status", expect="RUN", check=equals("run_state", "RUN"))
    await b.step("Z7", "scope_get_config", check=ok_true)


GROUP_FUNCS = {"session": g_session, "config": g_config, "rates": g_rates, "filters": g_filters, "acq": g_acq,
               "trigger": g_trigger, "counter": g_counter, "gen": g_gen, "bode": g_bode, "log": g_log, "data": g_data,
               "coreorder": g_coreorder, "loopback": g_loopback, "final": g_final}


# ---- reports --------------------------------------------------------------------------------------------------------


def markdown(run: Run, meta: dict[str, Any]) -> str:
    c = run.counts()
    info = run.facts.get("info", {})
    head = (f"**MCP battery** (`fnirsi1013d-battery`, {meta['started']}, "
            f"{'simulator' if meta.get('sim') else meta.get('port')}; fw {info.get('fw_version')} "
            f"{info.get('fw_build')} on FPGA {info.get('fpga_version')}, protocol {info.get('protocol')}; groups "
            f"{', '.join(meta['groups'])}): **{c['PASS']} PASS, {c['FAIL']} FAIL, {c['SKIP']} SKIP** in "
            f"{meta['seconds']:.0f} s; record `{meta['json']}`.")
    lines = [head, ""]
    groups: dict[str, dict[str, int]] = {}
    for s in run.steps:
        g = groups.setdefault(s.group, {"PASS": 0, "FAIL": 0, "SKIP": 0})
        g[s.status] += 1
    lines.append("| group | pass | fail | skip |")
    lines.append("|---|---|---|---|")
    for g, n in groups.items():
        lines.append(f"| {g} | {n['PASS']} | {n['FAIL']} | {n['SKIP']} |")
    bad = [s for s in run.steps if s.status != "PASS"]
    if bad:
        lines += ["", "Not passed:", ""]
        for s in bad:
            lines.append(f"- {s.status} [{s.label}] {s.tool}: expected {s.expect}; {s.detail}")
    if "core_order" in run.facts:
        row = run.facts["core_order"]
        lines += ["", "Core delay (divider: d): " + "; ".join(
            f"{c} " + ", ".join(f"{d}: {v:+d}" for d, v in r.items()) for c, r in row.items())]
    if run.facts.get("sweep_rate") is not None:
        lines.append(f"scope_sweep reached {run.facts['sweep_rate']:.1f} steps / s (100 asked).")
    return "\n".join(lines) + "\n"


# ---- entry ----------------------------------------------------------------------------------------------------------


def server_command(port: str | None, sim: str | None, max_tier: int = 3) -> list[str]:
    args = [sys.executable, "-m", "fnirsi1013d.server", "--max-tier", str(max_tier)]
    return args + (["--sim", sim] if sim is not None else ["--port", str(port)])


async def run_battery(groups: list[str], *, port: str | None = None, sim: str | None = None,
                      echo: Callable[[str], None] = print, coreorder_n: int = 12,
                      env: dict[str, str] | None = None, errlog: Path | None = None) -> Run:
    """Start the server, run ``groups`` in order and return the judged steps. The server's stderr goes to
    ``errlog`` (a file; the stdio client needs a real file handle)."""
    from mcp import StdioServerParameters

    cmd = server_command(port, sim)
    e = dict(os.environ if env is None else env)
    e["FNIRSI_MCP_IDLE_CLOSE_S"] = "0"  # the battery holds the port for its whole run
    params = StdioServerParameters(command=cmd[0], args=cmd[1:], env=e)
    run = Run()
    err = open(errlog, "w", encoding="utf-8") if errlog is not None else sys.stderr  # noqa: SIM115
    try:
        return await _run_session(params, groups, run, echo, sim is not None, coreorder_n, err)
    finally:
        if errlog is not None:
            err.close()


async def _run_session(params: Any, groups: list[str], run: Run, echo: Callable[[str], None], sim: bool,
                       coreorder_n: int, err: Any) -> Run:
    from mcp import ClientSession
    from mcp.client.stdio import stdio_client

    async with stdio_client(params, errlog=err) as (r, w):
        async with ClientSession(r, w) as session:
            await session.initialize()
            # Without a scope every step would fail the same way (bench 2026-11-06: 199 FAIL lines with the scope
            # switched off): one read first, and a run that cannot start says so once (main: exit status 2)
            pre = await session.call_tool("scope_info", {})
            if pre.isError:
                text = " ".join(c.text for c in pre.content if getattr(c, "type", "") == "text")
                raise RuntimeError(f"the scope does not answer scope_info, nothing was run: {text[:300]}")
            b = Battery(session, run, echo, sim=sim)
            # every group starts from the bench setup (channels, Filter Off, the generator, the CH1 trigger)
            for g in (["config"] if "config" not in groups and groups != ["session"] else []) + groups:
                b.group = g
                echo(f"== {g}")
                if g == "coreorder":
                    await g_coreorder(b, coreorder_n)
                else:
                    await GROUP_FUNCS[g](b)
    return run


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="fnirsi1013d-battery", description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    src = ap.add_mutually_exclusive_group()
    src.add_argument("--port", help="the scope's serial port (COM10, /dev/ttyACM0)")
    src.add_argument("--sim", nargs="?", const=SIM_ARGS, metavar="SIM_ARGS",
                     help=f"the in-process simulator (default arguments: {SIM_ARGS!r})")
    ap.add_argument("--groups", help=f"comma-separated groups (default {','.join(DEFAULT_GROUPS)}; optional "
                                     f"{','.join(OPTIONAL_GROUPS)}: CH1 straight on the calibration post)")
    ap.add_argument("--skip", help="comma-separated groups to leave out")
    ap.add_argument("--coreorder-captures", type=int, default=12, help="captures per divider in the coreorder group")
    ap.add_argument("--out", type=Path, help="directory for battery.json and battery.md (default "
                                             "<home>/battery/<date-time>)")
    ap.add_argument("--list", action="store_true", help="print the groups and exit")
    a = ap.parse_args(argv)
    if a.list:
        for g in DEFAULT_GROUPS + OPTIONAL_GROUPS:
            doc = (GROUP_FUNCS[g].__doc__ or "").strip().split("\n")[0]
            print(f"{g:10s} {'(optional) ' if g in OPTIONAL_GROUPS else ''}{doc}")
        return 0
    if a.port is None and a.sim is None:
        ap.error("give --port or --sim")
    groups = [g.strip() for g in a.groups.split(",")] if a.groups else list(DEFAULT_GROUPS)
    skip = {g.strip() for g in a.skip.split(",")} if a.skip else set()
    unknown = [g for g in groups + list(skip) if g not in GROUP_FUNCS]
    if unknown:
        ap.error(f"unknown group(s) {', '.join(unknown)}; known: {', '.join(GROUP_FUNCS)}")
    groups = [g for g in groups if g not in skip]
    from .paths import user_home

    started = _dt.datetime.now()
    out = a.out or user_home() / "battery" / started.strftime("%Y-%m-%d_%H%M%S")
    out.mkdir(parents=True, exist_ok=True)
    t0 = time.monotonic()
    try:
        run = anyio.run(lambda: run_battery(groups, port=a.port, sim=a.sim, coreorder_n=a.coreorder_captures,
                                            errlog=out / "server.log"))
    except Exception as e:  # noqa: BLE001 - the server did not start or the session broke
        while isinstance(e, BaseExceptionGroup) and e.exceptions:  # anyio's task group wraps the cause
            e = e.exceptions[0]
        print(f"fnirsi1013d-battery: cannot run: {type(e).__name__}: {e}", file=sys.stderr)
        return 2
    meta = {"started": started.isoformat(timespec="seconds"), "port": a.port, "sim": a.sim, "groups": groups,
            "seconds": time.monotonic() - t0, "bench": BENCH, "json": str(out / "battery.json")}
    rec = {"meta": meta, "facts": {k: v for k, v in run.facts.items() if not callable(v)}, "counts": run.counts(),
           "steps": [s.__dict__ for s in run.steps]}
    (out / "battery.json").write_bytes(json.dumps(rec, indent=1, default=_json_default).encode())
    md = markdown(run, meta)
    (out / "battery.md").write_bytes(md.encode())
    c = run.counts()
    print(f"== {c['PASS']} PASS, {c['FAIL']} FAIL, {c['SKIP']} SKIP; {out / 'battery.md'}")
    return 1 if c["FAIL"] else 0


def _json_default(o: Any) -> Any:
    if isinstance(o, (np.integer,)):
        return int(o)
    if isinstance(o, (np.floating,)):
        return float(o)
    if isinstance(o, float) and not math.isfinite(o):
        return None
    return str(o)


if __name__ == "__main__":
    sys.exit(main())
