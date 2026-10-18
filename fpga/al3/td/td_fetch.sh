#!/bin/bash
# Fetch TD_5.0.3_28716_NL_Linux.zip from the Sipeed download station (no account needed).
# Files >= 10 MB there are captcha-gated, and a human must read the captcha. Two steps:
#   td_fetch.sh captcha          -> saves the captcha image (path printed) and a cookie jar
#   td_fetch.sh download <code>  -> verifies the code and downloads the zip with that cookie
# Alternative without this script: open https://dl.sipeed.com/shareURL/TANG/Premier/IDE in a
# browser, download the zip, and pass its path to td_install.sh.
set -e
DL=${TD_DL_DIR:-$HOME/downloads/anlogic}
FILE=TANG/Premier/IDE/TD_5.0.3_28716_NL_Linux.zip
BASE=https://dl.sipeed.com
mkdir -p "$DL"
JAR="$DL/sipeed.cookies"
case "$1" in
  captcha)
    curl -sf -c "$JAR" -b "$JAR" -o "$DL/captcha.png" "$BASE/imgCode"
    echo "captcha image: $DL/captcha.png (Windows: \\wsl\$\Ubuntu$DL/captcha.png)"
    echo "then run: $0 download <code>" ;;
  download)
    : "${2:?usage: $0 download <code>}"
    curl -sf -c "$JAR" -b "$JAR" "$BASE/checkVerify?verify_code=$2"; echo
    curl -f -c "$JAR" -b "$JAR" -C - -o "$DL/TD_5.0.3_28716_NL_Linux.zip" \
      "$BASE/file/download?verify_code=$2&file_url=$FILE"
    ls -l "$DL/TD_5.0.3_28716_NL_Linux.zip"
    # A failed captcha returns a 31-byte JSON {"code":-1,...} instead of a zip.
    head -c 2 "$DL/TD_5.0.3_28716_NL_Linux.zip" | grep -q PK || { echo "not a zip (wrong code?)"; exit 1; }
    sha256sum "$DL/TD_5.0.3_28716_NL_Linux.zip" ;;
  *) echo "usage: $0 captcha | download <code>"; exit 2 ;;
esac
