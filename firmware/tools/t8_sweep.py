"""T8 bench helper: read raw ADC codes on one channel at every range with a known DC voltage applied.
Usage: .venv python firmware/tools/t8_sweep.py <applied_volts> [CH1|CH2] [COM10]
Writes backups/t8_<ch>_<volts>V.json. Requires the mcp venv (run from mcp/ or with PYTHONPATH=mcp/src)."""
import sys, time, json, os, numpy as np
ROOT=os.path.abspath(os.path.join(os.path.dirname(__file__),"..",".."))
from fnirsi1013d.scope import Scope
V=float(sys.argv[1]); CH=sys.argv[2] if len(sys.argv)>2 else "CH1"; PORT=sys.argv[3] if len(sys.argv)>3 else "COM10"
rows=[]
with Scope.connect(PORT) as sc:
    fw=sc.identify().fw_version.rstrip(b'\0').decode(); print("fw", fw)
    if hasattr(sc,'legacy_take_remote'): sc.legacy_take_remote(watchdog_s=120)
    elif hasattr(sc,'take_remote'): sc.take_remote(watchdog_s=120)
    sc.set_timebase(divider=1999)
    sc.set_trigger(mode=0, pretrigger_samples=1500, total_samples=3000)
    for r in range(sc.info.n_ranges):
        cfg=sc.set_channel(CH, range_index=r, coupling=0); time.sleep(0.15)
        sc.arm(); sc.wait(timeout_s=3)
        hdr,cores=sc.read_raw(0x03 if CH=="CH1" else 0x0C, addr_mode=1)
        ks=sorted(cores); a=np.asarray(cores[ks[0]]).astype(float); b=np.asarray(cores[ks[1]]).astype(float)
        cap=sc.read_capture((CH,)); cd=cap.channels[CH]
        row=dict(range=r, hw_range=cfg.hw_range, uv_per_lsb_q8=cfg.uv_per_lsb_q8, zero_code_q8=cfg.zero_code_q8, offset_pwm=cfg.offset_pwm,
                 center_cal=cfg.center_cal, dcoffset=cfg.dcoffset, probe_index=cfg.probe_index,
                 meanA=round(float(a.mean()),2), meanB=round(float(b.mean()),2), stdA=round(float(a.std()),2), stdB=round(float(b.std()),2),
                 client_volts_mean=round(float(np.mean(cd.volts)),4))
        rows.append(row); print(row)
    cal=sc.get_cal(CH, cached=False); print("CAL:", cal)
    sc.set_channel(CH, range_index=0, coupling=0)
    if hasattr(sc,'legacy_release'): sc.legacy_release()
    elif hasattr(sc,'release'): sc.release()
out=os.path.join(ROOT,"backups",f"t8_{CH}_{V:.3f}V.json")
json.dump(dict(fw=fw, channel=CH, applied_volts=V, rows=rows, cal=str(cal)), open(out,'w'), indent=1); print("saved", out)
