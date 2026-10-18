"""T8 offset sweep: with a known DC voltage applied, read raw codes per core on ranges 0..5 at several dcoffset values.
Usage: python firmware/tools/t8_offset_sweep.py <applied_volts> [CH1|CH2] [COM10] [offsets e.g. 0,44,-44,100]
Writes backups/t8off_<ch>_<volts>V.json and restores the original channel settings."""
import sys, time, json, os, numpy as np
from fnirsi1013d.scope import Scope
ROOT=os.path.abspath(os.path.join(os.path.dirname(__file__),"..",".."))
V=float(sys.argv[1]); CH=sys.argv[2] if len(sys.argv)>2 else "CH1"; PORT=sys.argv[3] if len(sys.argv)>3 else "COM10"
OFFS=[int(x) for x in (sys.argv[4] if len(sys.argv)>4 else "0,44,-44,100").split(",")]
rows=[]
with Scope.connect(PORT) as sc:
    fw=sc.identify().fw_version.rstrip(b'\0').decode(); print("fw", fw)
    if hasattr(sc,'legacy_take_remote'): sc.legacy_take_remote(watchdog_s=180)
    elif hasattr(sc,'take_remote'): sc.take_remote(watchdog_s=180)
    orig=sc.get_channel(CH, cached=False)
    sc.set_timebase(divider=1999); sc.set_trigger(mode=0, pretrigger_samples=1500, total_samples=3000)
    mask=0x03 if CH=="CH1" else 0x0C
    for r in range(6):
        for off in OFFS:
            try:
                cfg=sc.set_channel(CH, range_index=r, coupling=0, dcoffset=off)
            except Exception as e:
                print(f"range {r} dcoffset {off}: {str(e)[:80]}"); continue
            time.sleep(0.15); sc.arm(); sc.wait(timeout_s=3)
            hdr,cores=sc.read_raw(mask, addr_mode=1); ks=sorted(cores)
            a=np.asarray(cores[ks[0]]).astype(float); b=np.asarray(cores[ks[1]]).astype(float)
            row=dict(range=r, hw_range=cfg.hw_range, dcoffset=cfg.dcoffset, offset_pwm=cfg.offset_pwm, center_cal=cfg.center_cal,
                     zero_code_q8=cfg.zero_code_q8, uv_per_lsb_q8=cfg.uv_per_lsb_q8,
                     meanA=round(float(a.mean()),2), meanB=round(float(b.mean()),2), stdA=round(float(a.std()),2), stdB=round(float(b.std()),2),
                     satA=int(((a==0)|(a==255)).sum()), satB=int(((b==0)|(b==255)).sum()))
            rows.append(row); print(row)
    cal=sc.get_cal(CH, cached=False)
    sc.set_channel(CH, range_index=orig.range, coupling=orig.coupling, dcoffset=orig.dcoffset)
    if hasattr(sc,'legacy_release'): sc.legacy_release()
    elif hasattr(sc,'release'): sc.release()
out=os.path.join(ROOT,"backups",f"t8off_{CH}_{V:.3f}V.json")
json.dump(dict(fw=fw, channel=CH, applied_volts=V, offsets=OFFS, rows=rows, cal=str(cal)), open(out,'w'), indent=1); print("saved", out)
