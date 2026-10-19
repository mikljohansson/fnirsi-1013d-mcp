#!/usr/bin/env python3
"""Mutation runs for the 0x1536 sequencer (fpga/al3/src/zaklad.v): each mutation is one textual
replacement in a copy of zaklad.v; every mutant must make `ONLY=seg tb/run.sh` fail.

Usage (in WSL): python3 -I tb/mutate_seg.py [--jobs N] [--p1 N] [names...]
Work dirs: ~/al3-mut/<name>; summary at the end. Without names, all mutations run.
"""
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "src", "zaklad.v")

MUTATIONS = [
    ("remap_off", "wire [12:0] sample_write_address_phys = (sample_write_address & seg_mask_s) | (seg_base_s & ~seg_mask_s);",
     "wire [12:0] sample_write_address_phys = sample_write_address;"),
    ("slot_no_advance", "seg_base_s <= seq_wrap_s ? 13'd0 : seg_base_s + seg_mask_s + 13'd1;",
     "seg_base_s <= seq_wrap_s ? 13'd0 : seg_base_s;"),
    ("no_wrap", "                  seq_wrap <= 1;\n", "                  seq_wrap <= 0;\n"),
    ("offset_unmasked", "tab_d  <= { 1'b1, 9'd0, sn_trig, sn_ta & q_mask, sn_ts };",
     "tab_d  <= { 1'b1, 9'd0, sn_trig, sn_ta, sn_ts };"),
    ("ts_every_edge", "if(~st_s & adv_d & swe_d)\n        ts_cap <= ts_d;", "if(~st_s & swe_d)\n        ts_cap <= ts_d;"),
    ("ts_no_delay", "        ts_cap <= ts_d;", "        ts_cap <= ts_cnt;"),
    ("trig_not_restarted", "wire       cap_rst200  = sample_system_reset | (swc_rise_q & seq_rst_q);",
     "wire       cap_rst200  = sample_system_reset;"),
    ("ext_not_restarted", "      if(cap_rst200 == 1)\n        ext_pending <= 0;", "      if(sample_system_reset == 1)\n        ext_pending <= 0;"),
    ("no_first_write", "pp_acc <= q_first ? 64'd0 : acc_q;", "pp_acc <= acc_q;"),
    ("no_saturation", "pp_sat = s[16] ? 16'hFFFF : s[15:0];", "pp_sat = s[15:0];"),
    ("no_carry16", "wire [15:0] pp_w1 = pp_s1[15:0] + { 15'd0, pp_s0[16] };", "wire [15:0] pp_w1 = pp_s1[15:0];"),
    ("start_off_by_one", "pp_start    <= sn_ta[10:0] - q_pre[10:0];", "pp_start    <= sn_ta[10:0] - q_pre[10:0] + 11'd1;"),
    ("no_position_base", "                    q_base <= q_base_nx;", "                    q_base <= q_base;"),
    ("untriggered_added", "                else if(sn_trig)\n                  begin\n                    q_post  <= 1;",
     "                else if(1'b1)\n                  begin\n                    q_post  <= 1;"),
    ("holdoff_ignored", "            else if(q_hz)\n", "            else if(1'b1)\n"),
    ("no_invalidate", "                    tab_d  <= 64'd0;", "                    tab_d  <= aw_d;"),
    ("done_leaks", "o_mcu_data[0] <= q_active ? q_done : ~sample_write_enable;", "o_mcu_data[0] <= q_done | ~sample_write_enable;"),
    ("clamp16_off", "seq_sh32 = (|t[31:16]) ? 16'hFFFF : t[15:0];", "seq_sh32 = t[15:0];"),
    ("drain_early", "            if(~pp_i & ~pp_busy)", "            if(~pp_i)"),
    ("pointer_not_advanced", "         | ((mcu_command >= 8'h2B) & (mcu_command <= 8'h32))))", "         ))"),
    ("count_not_n", "q_last  <= q_pass_end;", "q_last  <= 1'b1;"),
]


def main():
    args = sys.argv[1:]
    jobs = 8
    p1 = 2
    if "--jobs" in args:
        i = args.index("--jobs"); jobs = int(args[i + 1]); del args[i:i + 2]
    if "--p1" in args:
        i = args.index("--p1"); p1 = int(args[i + 1]); del args[i:i + 2]
    src = open(SRC, encoding="utf-8").read()
    sel = [m for m in MUTATIONS if not args or m[0] in args]
    base = os.path.expanduser("~/al3-mut")
    procs = []
    results = {}
    pending = list(sel)
    while pending or procs:
        while pending and len(procs) < jobs:
            name, old, new = pending.pop(0)
            n = src.count(old)
            if n != 1:
                results[name] = "pattern found %d times" % n
                continue
            wd = os.path.join(base, name)
            os.makedirs(wd, exist_ok=True)
            dut = os.path.join(wd, "zaklad_mut.v")
            open(dut, "w", encoding="utf-8").write(src.replace(old, new))
            env = dict(os.environ, DUT=dut, SIM_WORK=wd, ONLY="seg", SEG_P1=str(p1))
            log = open(os.path.join(wd, "run.log"), "w")
            p = subprocess.Popen(["bash", os.path.join(HERE, "run.sh"), "3"], env=env, stdout=log,
                                 stderr=subprocess.STDOUT)
            procs.append((name, p, log, time.time()))
        time.sleep(2)
        for t in list(procs):
            name, p, log, t0 = t
            if p.poll() is not None:
                log.close()
                procs.remove(t)
                txt = open(os.path.join(base, name, "run.log"), encoding="utf-8", errors="replace").read()
                caught = "SOMETHING FAILED" in txt
                why = []
                for line in txt.splitlines():
                    if line.startswith("FAIL") or "-> FAIL" in line:
                        why.append(line.strip()[:110])
                results[name] = ("caught" if caught else "NOT CAUGHT") + " (%d s)" % (time.time() - t0) + \
                    ("; " + why[0] if why else "")
                print("%-22s %s" % (name, results[name]), flush=True)
    print("summary:")
    for name, _, _ in sel:
        print("  %-22s %s" % (name, results.get(name)))
    return 0 if all(v.startswith("caught") for v in results.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
