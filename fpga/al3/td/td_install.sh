#!/bin/bash
# Headless install of Anlogic TD 5.0.3 Linux into the WSL filesystem (not /mnt/d, not the repo).
# Usage: td_install.sh <path/to/TD_5.0.3_28716_NL_Linux.zip> [<Anlogic.lic>]
# Env:   TD_ROOT (default ~/anlogic)
set -e
ZIP=${1:?usage: $0 <TD zip> [licence]}
HERE=$(cd "$(dirname "$0")" && pwd)
LIC=${2:-$HERE/../license/Anlogic.lic}
ROOT=${TD_ROOT:-$HOME/anlogic}
LOG=$ROOT/install.log
mkdir -p "$ROOT"
echo "== sha256"; sha256sum "$ZIP" | tee "$ROOT/zip.sha256"
# Treat the archive as untrusted: inspect the listing before extracting.
echo "== listing check"
unzip -Z1 "$ZIP" > "$ROOT/zip.listing"
echo "entries: $(wc -l < "$ROOT/zip.listing")"
if grep -nE '(^/|(^|/)\.\.(/|$))' "$ROOT/zip.listing"; then echo "unsafe paths in archive, abort"; exit 1; fi
echo "top-level dirs:"; cut -d/ -f1 "$ROOT/zip.listing" | sort -u
TOP=$(cut -d/ -f1 "$ROOT/zip.listing" | sort -u | head -1)
EX=$ROOT/extract
rm -rf "$EX"; mkdir -p "$EX"
echo "== extract to $EX"; unzip -q "$ZIP" -d "$EX"
rm -rf "$ROOT/$TOP"; mv "$EX/$TOP" "$ROOT/$TOP"; rmdir "$EX"
TD=$ROOT/$TOP
chmod -R u+x "$TD/bin" 2>/dev/null || true
echo "== licence"; mkdir -p "$TD/license"; cp "$LIC" "$TD/license/Anlogic.lic"
grep '^FEATURE' "$TD/license/Anlogic.lic" | awk '{print "feature",$2,"vendor",$3,"expires",$4}'
echo "== missing shared libs before shims"
ldd "$TD/bin/td" | grep 'not found' || echo "(none)"
# Stub every missing GUI lib with an empty .so of the same soname; batch mode never calls them.
SHIM=$ROOT/shim; mkdir -p "$SHIM"
echo 'void __td_stub(void){}' > "$SHIM/stub.c"
for so in $(ldd "$TD/bin/td" | awk '/not found/{print $1}'); do
  gcc -shared -fPIC -Wl,-soname,"$so" -o "$SHIM/$so" "$SHIM/stub.c"; echo "stubbed $so"
done
echo "== missing shared libs with shims"
LD_LIBRARY_PATH="$SHIM:$TD/lib" ldd "$TD/bin/td" | grep 'not found' || echo "(none)"
echo "TD_HOME=$TD"; echo "TD_SHIM=$SHIM"
