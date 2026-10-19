"""CAL_PROBE (fw 0.2.67, protocol 1.21 provisional, docs/protocol.md section 14 row 61): run the calibration wizard's
zero measurement of one channel and hardware range from the host and print every point the wizard measures.

Usage: python firmware/tools/cal_probe.py [COM10] [CH1|CH2|both] [ranges, e.g. 0 or 0,1,2 or all] [repeats]

The UI acquisition is stopped for the run and started again afterwards if it was running. Nothing is stored on the
scope. Writes backups/calprobe_<time>.json with the decoded responses."""
import json
import os
import struct
import sys
import time

from fnirsi1013d.scope import Scope

CAL_PROBE = 0x0402
POINTS = ["coarse hi (500)", "coarse lo (1200)", "gated lo", "gated hi", "gated zero", "full zero", "full hi", "full lo"]
RC = {0: "OK", 4: "RESPONSE", 5: "CLIP", 6: "NOISE", 7: "ZERO", 8: "SPREAD", 9: "SLOPE", 10: "DRIFT", 11: "CLASS",
      18: "TIMING"}
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))


def decode(b: bytes) -> dict:
    if len(b) != 248:
        raise ValueError(f"CAL_PROBE answered {len(b)} bytes, expected 248")
    ch, rng, rc, fpt, fcls, fcore, flags, fval, fwithin, centre, pwm, ebad, erec = struct.unpack_from(
        "<BBhBBBBiIHHHH", b, 0)
    pts = []
    for i in range(8):
        o = 24 + 28 * i
        ppwm, cls, convs = struct.unpack_from("<HBB", b, o)
        cores = []
        for k in range(2):
            mean16, rms16, within16, lo, hi, clip = struct.unpack_from("<iHHBBH", b, o + 4 + 12 * k)
            cores.append(dict(mean=mean16 / 16, rms=rms16 / 16, within=within16 / 16, lo=lo, hi=hi, clipped=clip))
        pts.append(dict(name=POINTS[i], pwm=ppwm, cls="full" if cls == 0 else "gated", convs=convs, A=cores[0],
                        B=cores[1]))
    return dict(channel=ch + 1, range=rng, rc=rc, rc_name=RC.get(rc, str(rc)),
                fail_point=None if fpt == 0xFF else POINTS[fpt], fail_class="full" if fcls == 0 else "gated",
                fail_core="AB"[fcore & 1], fail_value=fval, fail_within16=fwithin, garbled=bool(flags & 1),
                no_centre=bool(flags & 2), centre_pwm=centre, zero_pwm=pwm, enc_flagged=ebad, enc_recovered=erec,
                points=pts)


def show(d: dict) -> None:
    print(f"CH{d['channel']} range {d['range']} ({d['range'] + 1} in the wizard): {d['rc_name']}"
          f"  fail point {d['fail_point']} class {d['fail_class']} core {d['fail_core']} value {d['fail_value']}"
          f" within {d['fail_within16'] / 16:.1f}  centre {d['centre_pwm']} zero PWM {d['zero_pwm']}"
          f"  enc flagged {d['enc_flagged']} recovered {d['enc_recovered']}{'  GARBLED' if d['garbled'] else ''}")
    for p in d["points"]:
        if not p["convs"]:
            continue
        a, b = p["A"], p["B"]
        print(f"  {p['name']:17s} PWM {p['pwm']:5d} {p['cls']:5s} x{p['convs']:<3d}"
              f" A {a['mean']:6.1f} rms {a['rms']:5.2f} in {a['within']:5.2f} [{a['lo']:3d}..{a['hi']:3d}] clip {a['clipped']:<5d}"
              f" B {b['mean']:6.1f} rms {b['rms']:5.2f} in {b['within']:5.2f} [{b['lo']:3d}..{b['hi']:3d}] clip {b['clipped']}")


def main() -> int:
    port = sys.argv[1] if len(sys.argv) > 1 else "COM10"
    chs = {"CH1": [0], "CH2": [1], "both": [0, 1]}[sys.argv[2] if len(sys.argv) > 2 else "both"]
    arg = sys.argv[3] if len(sys.argv) > 3 else "0"
    ranges = list(range(6)) if arg == "all" else [int(x) for x in arg.split(",")]
    repeats = int(sys.argv[4]) if len(sys.argv) > 4 else 1
    out = []
    with Scope.connect(port) as sc:
        was_running = sc.status().run_state == 1
        sc.set_acq_state(False)
        try:
            for _ in range(repeats):
                for ch in chs:
                    for r in ranges:
                        d = decode(sc.t.request(CAL_PROBE, bytes([ch, r]), 10.0).payload)
                        show(d)
                        out.append(d)
        finally:
            if was_running:
                sc.set_acq_state(True)
    path = os.path.join(ROOT, "backups", time.strftime("calprobe_%Y%m%d-%H%M%S.json"))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(out, f, indent=1)
    print("saved", path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
