# Headless Anlogic TD setup in WSL Ubuntu 24.04

Working, verified 2026-10-18: TD 4.6.8 SP1 (V4.6.116866, the version Atlan4 used) runs headless in
WSL and rebuilds `src/` into a `.bin` that is **byte-identical** to Atlan4's shipped
`bitstreams/atlan4-FPGA-0x1532.bin`. Results are in `README.md`, section "Rebuild results".

Run WSL commands from Windows PowerShell as `wsl -d Ubuntu -- bash /mnt/d/...script.sh`. Git Bash
mangles `/mnt` paths, and PowerShell mangles inline `bash -c` quoting, so put anything non-trivial in a script.

## Layout

| Path (WSL) | What |
|---|---|
| `~/downloads/anlogic/TD_4.6.8_SP1_116866_NL.zip` | Installer copy, untouched |
| `~/anlogic/TD_4.6.8_SP1_116866_NL/` | TD install (`TD_HOME`, the `build.sh` default) |
| `~/anlogic/shim/` | Empty stub `libgtk-x11-2.0.so.0` and `libgdk-x11-2.0.so.0` (`TD_SHIM`) |
| `~/anlogic/zip.sha256`, `zip.listing` | Archive hash and listing recorded at install |
| `~/al3-build/` | Build work dir (WSL-local, fast); results are copied to `fpga/al3/build/` (gitignored) |

## 1. Installer

The owner downloaded these from Anlogic (account needed; agents must not log in):

| File | Bytes | sha256 | Content |
|---|---|---|---|
| `TD_4.6.8_SP1_116866_NL.zip` | 68,541,435 | `12f8c2b27393fe43ad9d38563dff3d68e0ee0d3349fb35a9ac9a438a4f933f15` | **Linux** build: `bin/td` (ELF, 80.6 MB), `arch/` incl. `al3_10.db`, `al3_s10.db`, `al3_macro.v`; 226 entries, no absolute or `..` paths. Used. |
| `TD_4.6.8_CPLD_Release_96021_NL.zip` | 68,541,883 | `f99d3c9cc05cdd6e3032aca01551b6a8ff2958e52d3e1fbbd99b7942e29b65f3` | Also Linux, same layout, a different `bin/td` plus `bin/settings.ini`. Not installed. |

Both are Linux tarballs in zip form, not Windows MSIs, so no Windows install was needed.
TD 5.x from the Sipeed mirror is captcha-gated (see "Other routes" below).

## 2. Prerequisites (root; no password in this WSL)

```powershell
wsl -d Ubuntu -u root -- bash -c "apt-get update && apt-get install -y unzip gcc libsm6 libice6"
```

`libsm6`/`libice6` are real X11 session libs (no X server needed). TD 4.6.8 resolves `SmcInteractDone`
at load time, so an empty stub for `libSM.so.6` fails with `symbol lookup error`. Donwulff's TD 5.0.3
needed only the two GTK stubs.

## 3. Install

```powershell
wsl -d Ubuntu -- bash -c "mkdir -p ~/downloads/anlogic && cp /mnt/c/Users/miklj/Downloads/TD_4.6.8_SP1_116866_NL.zip ~/downloads/anlogic/"
wsl -d Ubuntu -- bash /mnt/d/src/fnirsi-1013d-ai/fpga/al3/td/td_install.sh ~/downloads/anlogic/TD_4.6.8_SP1_116866_NL.zip
```

`td/td_install.sh` does, in order:

1. Records the sha256 and the `unzip -Z1` listing, and aborts on absolute or `..` paths.
2. Extracts into an empty `~/anlogic/extract/` and moves the top directory to `~/anlogic/`.
3. Copies `fpga/al3/license/Anlogic.lic` (gitignored; FEATURE TD, expires 2027-09-30, HOST_ID ANY) to
   `<TD>/license/Anlogic.lic`. TD logs `Open license file <TD>/license/Anlogic.lic`; no environment variable is used.
4. Stubs every library `ldd bin/td` reports missing with an empty `.so` of the same soname in
   `~/anlogic/shim/`. Install `libsm6 libice6` first (step 2) so only the two GTK libs get stubbed.
   If the stubs for `libSM.so.6`/`libICE.so.6` exist from an earlier run, delete them.
5. Re-runs `ldd` with `LD_LIBRARY_PATH=shim:<TD>/lib` and prints `(none)` when nothing is missing.

## 4. Build

```powershell
wsl -d Ubuntu -- bash /mnt/d/src/fnirsi-1013d-ai/fpga/al3/build.sh
```

`build.sh` copies `src/` unchanged into `~/al3-build`, fills `build.tcl` (TD path, user code), runs
`LD_LIBRARY_PATH=$TD_SHIM:$TD_HOME/lib $TD_HOME/bin/td flow.tcl`, prints the wall-clock time, hashes,
and copies `FPGA.bit`, `FPGA.bin`, `FPGA_*.area`, `FPGA_phy.timing`, `FPGA_phy.tsm`, `build.log` to
`fpga/al3/build/`. About 11 to 16 s wall clock.

`build.tcl` replays the top-level commands of Atlan4's own TD GUI logs
(`research/atlan4-extracted/FPGA/FPGA/td_2025-11-1*.log`):
`open_project FPGA.al`, `import_device al3_10.db -package LQFP144`, `elaborate -top fnirsi_1013D`,
`read_adc`, `optimize_rtl`, `read_sdc`, `optimize_gate -packarea`, `place`, `route`, area/timing reports,
`bitgen -bit FPGA.bit -version 0X00 -g ucode:<32 bits> -bin FPGA.bin`. Lessons:

- The GUI log also shows `legalize_phy_inst`, `start_timer` and a second `read_sdc`, but those run nested
  inside `optimize_gate`/`place`/`route`. Issuing them as separate commands makes `route` segfault (exit 139).
- The bitgen user code is the only input that changes the output. It is 8 zero bits, then the 8-bit
  `<UCode>` from `FPGA.al` (`01010110`), then 16 zero bits; `build.sh` reads it from `FPGA.al`.
  With Atlan4's Nov 2025 value (`11010010`) the `.bin` differs in 3 bytes (user-code word and its CRC).
- Placement is deterministic: three replay builds produced identical configuration frames. No seed option was needed.
- `FLOW=donwulff ./build.sh` selects `build_donwulff.tcl` (concatenated `read_verilog -lib al3_macro.v`,
  `map`, `pack`). With 4.6.8 it reaches `route` and `report_area`, then `report_io` asserts
  (`Design::GetFloorColumn: Assertion placer_ failed`). Kept only for TD 5.x.
- "sh: Bad fd number" lines in the log are TD running bashisms under dash; cosmetic.

Compare with the reference:

```powershell
wsl -d Ubuntu -- python3 -I /mnt/d/src/fnirsi-1013d-ai/fpga/al3/td/compare_bin.py /mnt/d/src/fnirsi-1013d-ai/fpga/al3/build/FPGA.bin /mnt/d/src/fnirsi-1013d-ai/fpga/al3/bitstreams/atlan4-FPGA-0x1532.bin
```

Never flash a build from here without the gates in `CLAUDE.md`.

## Other routes (not used)

- **TD 5.0.3 Linux** (`TD_5.0.3_28716_NL_Linux.zip`, 413.91 MB) on the Sipeed station
  `https://dl.sipeed.com/shareURL/TANG/Premier/IDE`. Files of 10 MB or more need a captcha that a human
  must solve: download in a browser, or `td/td_fetch.sh captcha` then `td/td_fetch.sh download <code>`.
  Install with `td_install.sh` and run `TD_HOME=~/anlogic/TD_5.0.3_28716_NL FLOW=donwulff ./build.sh`.
  The MEGA mirror linked there does not expose the TANG folder (undecryptable node keys); chipdebug.com
  and Baidu need a login.
- **Windows**: TD 5.0.4 MSI from the same Sipeed folder (captcha), or an Anlogic MSI; install, put
  `Anlogic.lic` in `<TD>\license\`, open `src\FPGA.al`. An unofficial third-party repackage
  `SayCV/gh-cdn-cvp` release `anlogic-td-5.0.3_29524.zip` (477,119,587 B) exists; not downloaded or run.

## Install log, 2026-10-18

- WSL Ubuntu 24.04, kernel 6.6.87.2, 24 threads, 15 GB RAM. Installed `unzip`, `megatools` (for the
  failed MEGA probe), then `libsm6`, `libice6`. `gcc`, `curl`, `python3` were present.
- `td_install.sh` on the SP1 zip: listing clean, licence copied, `ldd` missing libgtk-x11, libgdk-x11,
  libSM, libICE; stubs built. First run failed `undefined symbol: SmcInteractDone`; installed `libsm6
  libice6` and deleted those two stubs.
- Donwulff flow: synthesis to route OK, `report_io` assertion. First replay flow: `route` segfault from
  nested commands. Fixed flow: success, `.bin` 3 bytes off (user code). With the `FPGA.al` user code:
  byte-identical.
