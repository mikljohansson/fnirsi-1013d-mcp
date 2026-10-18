"""Square-wave coupling check: with the calibration output on <CH>, read codes at range 3, dcoffset 0, both couplings."""
import sys, time, json, os, numpy as np
from fnirsi1013d.scope import Scope
ROOT=os.path.abspath(os.path.join(os.path.dirname(__file__),"..",".."))
CH=sys.argv[1] if len(sys.argv)>1 else "CH1"; PORT=sys.argv[2] if len(sys.argv)>2 else "COM10"
out=[]
with Scope.connect(PORT) as sc:
    print("fw", sc.identify().fw_version.rstrip(b'\0'))
    if hasattr(sc,'legacy_take_remote'): sc.legacy_take_remote(watchdog_s=120)
    elif hasattr(sc,'take_remote'): sc.take_remote(watchdog_s=120)
    orig=sc.get_channel(CH, cached=False)
    sc.set_gen(enable=True, freq_hz=1000, duty=0.5)
    sc.set_timebase(divider=199)   # 1 MS/s -> 3 periods in 3000 samples
    sc.set_trigger(source=0 if CH=="CH1" else 1, edge=0, level=150, mode=1, pretrigger_samples=1500, total_samples=3000)
    mask=0x03 if CH=="CH1" else 0x0C
    for coup in (0,1):
        for rng in (3,1):
            cfg=sc.set_channel(CH, range_index=rng, coupling=coup, dcoffset=0); time.sleep(0.4)
            sc.arm(); st=sc.wait(timeout_s=3)
            hdr,cores=sc.read_raw(mask, addr_mode=1); ks=sorted(cores)
            a=np.asarray(cores[ks[0]]).astype(float); b=np.asarray(cores[ks[1]]).astype(float)
            lo_a,hi_a=np.percentile(a,5),np.percentile(a,95); lo_b,hi_b=np.percentile(b,5),np.percentile(b,95)
            cap=sc.read_capture((CH,)); v=cap.channels[CH].volts
            row=dict(coupling=coup, range=rng, dcoffset=cfg.dcoffset, offset_pwm=cfg.offset_pwm, acq_state=st.acq_state,
                     lowA=round(lo_a,1), highA=round(hi_a,1), lowB=round(lo_b,1), highB=round(hi_b,1),
                     client_low_V=round(float(np.percentile(v,5)),3), client_high_V=round(float(np.percentile(v,95)),3))
            out.append(row); print(row)
    sc.set_channel(CH, range_index=orig.range, coupling=orig.coupling, dcoffset=orig.dcoffset)
    if hasattr(sc,'legacy_release'): sc.legacy_release()
    elif hasattr(sc,'release'): sc.release()
json.dump(out, open(os.path.join(ROOT,"backups",f"t8sq_{CH}.json"),'w'), indent=1); print("saved")
