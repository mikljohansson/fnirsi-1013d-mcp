import os, sys

D = sys.argv[1]
NW = "display: block; white-space: nowrap"


def lines(*ls):
    return "".join(f'<span style="{NW}">{l}</span>' for l in ls)


def sub(name, pairs, count_ok=1):
    p = os.path.join(D, name)
    s = open(p, encoding="utf-8").read()
    for a, b, *n in pairs:
        want = n[0] if n else 1
        assert s.count(a) == want, (name, a[:70], s.count(a))
        s = s.replace(a, b)
    open(p, "w", encoding="utf-8", newline="\n").write(s)
    print("edited", name)


G = '<span style="color: #35c26a">OK</span>'
GREY13 = 'style="font-size: 13px; color: #9a9a9a"'

# ------------------------------------------------------------------ Status (Main)
status_grid_old = open(os.path.join(D, "Main.dc.html"), encoding="utf-8").read()
start = status_grid_old.index('      <div style="display: grid; grid-template-columns: 44px repeat(3')
end = status_grid_old.index('      <div style="font-size: 13px; color: #9a9a9a; margin-top: auto">')
old_grid = status_grid_old[start:end]
new_grid = f'''      <div style="display: grid; grid-template-columns: 44px repeat(3, minmax(0, 1fr)); gap: 2px 12px; font-size: 16px; align-items: baseline; white-space: nowrap">
        <div></div><div {GREY13}>Core A-B at 0 V, corrected</div><div {GREY13}>Gain mismatch, corrected</div><div {GREY13}>Class ratio full / gated</div>
        <div></div><div {GREY13}>limit 0.5 code</div><div {GREY13}>limit 1 %</div><div {GREY13}>limit 2 %</div>
        <div style="color: #ffd400">CH1</div><div>0.3 code {G}</div><div>0.0 % {G}</div><div>1.1 % {G}</div>
        <div style="color: #39d2f0">CH2</div><div>0.2 code {G}</div><div style="color: #9a9a9a">not checked</div><div>0.9 % {G}</div>
      </div>
'''
sub("Main.dc.html", [
    (old_grid, new_grid),
    ('>Running 0.2.48 from RAM. A power cycle starts 0.2.25 from the card.<',
     '>' + lines("Running 0.2.48 from RAM.", "A power cycle starts", "0.2.25 from the card.") + '<'),
    ('>None since power-on. Up 1 h 42 min.<', '>' + lines("None since power-on.", "Up 1 h 42 min.") + '<'),
    ('>PC connected, 0 bad frames. 1 suspend, 0 reconnects.<',
     '>' + lines("PC connected.", "0 bad frames, 1 suspend,", "0 reconnects.") + '<'),
    ('>Charging. RTC module 31 °C.<', '>' + lines("Charging.", "RTC module 31 °C.") + '<'),
    ('Calibrated on fw 0.2.48, FPGA 0x1534, 12 min after power-on, 1 power-on ago. Details and limits: Calibration page.',
     'Calibrated on fw 0.2.48, FPGA 0x1534, 12 min after power-on, 1 power-on ago.'),
    ('Values update once a second. PC control stays active on this screen.', 'Updates every second'),
])

# ------------------------------------------------------------------ Calibration
cal_ch = {
    "CH1": [("Core A-B at 0 V", f"0.3 code {G}"), ("Gain mismatch", f"0.0 % full, 0.1 % gated {G}"),
            ("Class ratio", f"1.1 % full / gated {G}"), ("Noise RMS", f"0.13 gated, 0.75 full {G}"),
            ("DC shift", "centre 870, size 100, value 59136")],
    "CH2": [("Core A-B at 0 V", f"0.2 code {G}"), ("Gain mismatch", '<span style="color: #9a9a9a">not checked: use Check on post</span>'),
            ("Class ratio", f"0.9 % full / gated {G}"),
            ("Noise RMS", '0.13 gated, 2.1 full <span style="color: #ffd262">Check</span>'),
            ("DC shift", "centre 855, size 100, value 32512")],
}


def cal_rows(ch):
    o = ['<div style="grid-column: 1 / 3; color: #9a9a9a">After correction; limits 0.5 code, 1 %, 2 %</div>']
    for lab, val in cal_ch[ch]:
        o.append(f'<div style="color: #9a9a9a">{lab}</div><div>{val}</div>')
    return "\n        ".join(o) + "\n"


s = open(os.path.join(D, "Calibration.dc.html"), encoding="utf-8").read()
pairs = []
for ch, old_first in [("CH1", "corrected</span>: 1.3 % full, 0.1 % gated"), ("CH2", "corrected</span>: 0.9 % full, 2.1 % gated")]:
    i = s.index(old_first)
    a = s.rindex('        <div style="color: #9a9a9a">Gain mismatch</div>', 0, i)
    b = s.index("      </div>", i)
    pairs.append((s[a:b], "        " + cal_rows(ch)))
hdr_old = '<div style="color: #777777">Zero A / B gated</div><div style="color: #777777">Zero A / B full</div>'
hdr_new = ('<div style="color: #777777">Zero gated</div><div style="color: #777777">Zero full</div>'
           '<div></div><div></div><div></div><div style="color: #777777">A / B</div><div style="color: #777777">A / B</div>')
prov_old = s[s.index('    <div>Block v3 in sector 708'):s.index('  </div>', s.index('    <div>Block v3 in sector 708'))]
prov_new = ('    <div style="display: flex; flex-direction: column; gap: 2px; white-space: nowrap">'
            '<span>Block v3 in sector 708, checksum good, bound to the current offsets. '
            '<span style="color: #ffd262">Made on FPGA 0x1534; now 0x1535</span></span>'
            '<span>fw 0.2.48, 12 min after power-on, 1 power-on ago</span></div>\n')
pairs += [(hdr_old, hdr_new, 2), (prov_old, prov_new),
          ('min-width: 140px; padding: 0 18px', 'min-width: 100px; padding: 0 12px', 5)]
sub("Calibration.dc.html", pairs)

# ------------------------------------------------------------------ Log
def ev(t):
    return f'        <div style="color: #777777; text-align: right">{t}</div>'


sub("Log.dc.html", [
    ("last 12 of 51, device time", "last 10 of 51, device time"),
    (ev("6132.0 s") + "<div>Serial line settings 115200 8N1</div>\n", ""),
    ("<div>Bus reset, high speed, 4 ms after suspend</div>", "<div>Bus reset, 4 ms after suspend</div>"),
    ('>Suspend, POWER 0x60, frame 700</div>', '>Suspend</div>'),
    ("<div>Bus reset, high speed</div>", "<div>Bus reset</div>", 2),
    (ev("0.0 s") + "<div>Power-on, cold boot</div>\n", ""),
    (">Counters since power-on: 1 suspend, 0 resumes, 0 inferred resumes, 0 reconnects, 0 bad frames.<",
     ">" + lines("Since power-on: 1 suspend, 0 resumes,", "0 inferred resumes, 0 reconnects, 0 bad frames.") + "<"),
    (">A restart records its cause, program address and fault address here, and keeps them on the card through a power cycle.<",
     ">" + lines("A restart records its cause, program", "address and fault address here.") + "<"),
    (">7.4 GB. Boot sectors 16, 80 and 750 checked. Settings and calibration are written at power-off.<",
     ">" + lines("7.4 GB. Boot sectors 16, 80, 750 checked.", "Settings and calibration save at power-off.") + "<"),
    (">Screen pass 15 ms at 3000 samples. FFT 4096 points 2.7 ms. Readout 0.3 us per byte.<",
     ">" + lines("Screen pass 15 ms at 3000 samples.", "FFT 4096 points 2.7 ms.") + "<"),
    ('    <div style="margin-left: auto; font-size: 13px; color: #9a9a9a">The report holds identity, counters, this log and the calibration block as one text file.</div>\n', ""),
])

# ------------------------------------------------------------------ Settings
row = ('      <button type="button" style="display: flex; align-items: center; justify-content: space-between; height: 48px; '
       'padding: 0 12px; background: #0d0d0d; border: 1px solid #262626; border-radius: 4px; color: #e6e6e6; font: inherit; '
       'font-size: 16px; text-align: left">\n        <span>RTC module</span><span style="color: #35c26a">on</span>\n      </button>\n')
sub("Settings.dc.html", [
    ("height: 52px", "height: 48px", 10),
    ('<span>Confirmation prompts</span><span style="color: #35c26a">shown</span>',
     '<span>File message timeout</span><span style="color: #35c26a">0.7 s</span>'),
    (">Clock (RTC module fitted)</div>\n", ">Clock</div>\n" + row),
    ('<span>At power-on</span><span style="color: #35c26a">open firmware</span>',
     '<span>At power-on</span><span style="color: #35c26a">Open firmware</span>'),
    ('    <div style="margin-left: auto; font-size: 13px; color: #9a9a9a">Developer options (FEL start, debug overlay): Diagnostics, Log page.</div>\n', ""),
])
