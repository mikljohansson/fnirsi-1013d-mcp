"""Renders artboards to 800 x 480 PNGs with a headless Chromium (Google Chrome or Microsoft Edge on Windows, chromium /
google-chrome on Linux), one screenshot per .dc.html file.

usage: python render.py <dir> [name ...]    renders every <dir>/*.dc.html (or the named boards) to <dir>/<name>.png
"""
import glob, os, shutil, subprocess, sys

CANDIDATES = [
    r"C:\Program Files\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
    r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
    r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
]


def browser():
    for c in CANDIDATES:
        if os.path.exists(c):
            return c
    for name in ("chromium", "chromium-browser", "google-chrome", "chrome", "msedge"):
        p = shutil.which(name)
        if p:
            return p
    sys.exit("no Chromium browser found")


def render(exe, html, png):
    url = "file:///" + os.path.abspath(html).replace("\\", "/")
    subprocess.run([exe, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--window-size=800,480",
                    f"--screenshot={os.path.abspath(png)}", url], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print("rendered", os.path.basename(png))


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "."
    names = sys.argv[2:]
    files = ([os.path.join(d, n if n.endswith(".dc.html") else n + ".dc.html") for n in names] if names
             else sorted(glob.glob(os.path.join(d, "*.dc.html"))))
    exe = browser()
    for html in files:
        render(exe, html, html[:-len(".dc.html")] + ".png")


if __name__ == "__main__":
    main()
