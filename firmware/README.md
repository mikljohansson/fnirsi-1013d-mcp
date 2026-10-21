# Firmware

```
firmware/scope/       scope application, Atlan4 v1.00o5 source + plain Makefile
firmware/tools/       mksunxi.py, make_sd_image.py, fetch_gcc10.sh
firmware/bootloader/  (later) bootloader/updater work
```

## 1. Building the scope

Toolchain in WSL Ubuntu 24.04 (installed 2026-10-18):

| tool | version |
|---|---|
| arm-none-eabi-gcc (apt `gcc-arm-none-eabi`) | 13.2.1 20231009 (`15:13.2.rel1-2`) |
| arm-none-eabi binutils | 2.42 |
| GNU make / python3 | 4.3 / 3.12.3 |
| sunxi-tools (`sunxi-fel`) | 1.4.2+git20221128 |
| Atlan's toolchain for v1.00o5 (ELF `.comment`) | GCC 10.3.1 `15:10.3-2021.07-4` (Ubuntu 22.04 package) |

```
wsl -d <distro> -- bash -lc "cd /mnt/d/path/to/fnirsi-1013d-mcp/firmware/scope && make -j"
```

Outputs in `firmware/scope/build/`:

| file | what | upstream name |
|---|---|---|
| `fnirsi_1013d.elf` | linked ELF (with `-g`) | `fnirsi_1013d_scope.elf` |
| `fnirsi_1013d.bin` | eGON.EXE image, mksunxi-fixed. Goes to SD sector 750 or is loaded over FEL | `fnirsi_1013d_scope.bin` (the updater's `scope.bin`) |
| `fnirsi_1013d.map` | link map | - |

Targets: `make`, `make clean`, `make V=1`, `make fel`, `make info` (`objdump -h`), `make gcc10`.
Parallel builds and `-MMD -MP` dependency tracking work. The USB control protocol is always built in;
section 5 has its options. `CROSS_COMPILE=` selects another toolchain and `EXTRA_CFLAGS=` adds flags.
SD packages come from `tools/make_release_sd_package.py build` (it fetches Atlan's bootloader and updater).

### Reproducible images (fw 0.2.47)

The image depends only on the source, the toolchain and the commit. The identity is the short commit hash
(`PC_FW_BUILD`) and the commit time (`SOURCE_DATE_EPOCH`, UTC; also the RTC default in variables.c), both read
from git by the Makefile, never the wall clock or the checkout path. `build_id.c` (linked last) holds them as
data, so a new commit changes those 10 bytes and not the code or the layout. `-ffile-prefix-map` and
`-Wa,--debug-prefix-map` map the source directory to `.`, so the ELF is the same from every directory too.
In a git worktree whose `.git` file names a Windows path (`gitdir: D:/...`), WSL's git cannot follow it; the
Makefile translates it to `/mnt/d/...`. Where git is not readable at all, the build stops and asks for
`PC_FW_BUILD=<git rev-parse --short=8 HEAD> SOURCE_DATE_EPOCH=<git log -1 --format=%ct>`.

Cause of the old difference (0.2.44: main d4c0b716 vs worktree 08f9a5ed, 172,941 bytes): the worktree build had
hash 00000000 and its own wall-clock time, compiled into the code as constants; GCC folds the hex loop and the
literal pool differently for 0 (80 bytes less .text), which shifted everything linked after it. Check: build the
same commit in two clones at different paths and compare `sha256sum build-gcc10-pc/fnirsi_1013d.bin` (and `.elf`);
CI prints the image sha256 in the firmware job, which a local `make gcc10` of the same commit matches.

### What was reproduced from upstream

Atlan's NetBeans project builds the **Debug** configuration (`nbproject/Makefile-Debug.mk`,
`DEFAULTCONF=Debug`). Its "Release" configuration is a stale STM32 leftover with
`-mcpu=cortex-m3 -mthumb` and `stm32f103-64k.ld`, and cannot build this code. Donwulff found the
same (`research/notes/repo-donwulff.md`). The Makefile reproduces Debug exactly:

- C: `-Wall -Wno-write-strings -Wno-char-subscripts -fno-stack-protector -DNO_STDLIB=1
  -mcpu=arm926ej-s -O3 -mfloat-abi=soft -g`
- asm: `-x assembler-with-cpp -c -O0 -mcpu=arm926ej-s -mthumb -Wall -fmessage-length=0 -g`.
  The `.s` files still assemble as ARM code (checked with objdump).
- link: `gcc -o elf <objs in NetBeans order> -T fnirsi_1013d.ld -nostdlib -lgcc`. The link order
  sets the memory layout and is kept.
- post: `objcopy -O binary`, then `mksunxi`. The Makefile uses `tools/mksunxi.py`, which is
  byte-identical to the binary.

Upstream post-build steps left out: copying to Atlan's own `/media/dk/...` cards and
`../../FNIRSI_1013D_Firmware-main/...`, and `sudo sunxi-fel` after every build. Use `make fel`
for that. `tools/make_release_sd_package.py build` replaces `flashfilepacker -l 0x5BC00`.

Changes to the copied source, all needed only for the build system:

1. `fnirsi_1013d.ld` matches `*start.o` instead of `build/Debug/GNU_ARM-Linux/start.o`.
2. `build_time.h` is generated into `build/` and is not part of the source tree.
3. `strlen.s` bug fixed: the loop label was on `mov r1, r0`, so it returned a pointer. Upstream
   never linked it. GCC 13 turns the counting loops in `my_strlen` (`PC_interface.c`) and
   `write_text` (`test.c`) into `strlen` calls, which do not exist without libc.
   `strlen.s` and `strcpy.s` are therefore linked from `build/libsupport.a`. Archive members are
   pulled in only when referenced, so GCC 10 links nothing extra.

Not built, same as upstream: `font_6.c`, `font_7.c`, `font_8.c`, `malloc.c`.

Warnings under GCC 13: 33 in total. 27 are unused variables and 3 are set-but-unused. One is an
implicit declaration of `usb_CDC_in_ep_callback` in `menu.c`. That missing prototype is an upstream
bug, and GCC 14 makes it an error by default.

### Verification against Atlan's binary

- **Byte-identical with Atlan's toolchain (history, up to 0.2.46).** `make check-reference` built with
  `PC_PROTOCOL=0` (section 5), which proved that the protocol hooks left the base untouched when disabled.
  The maintainer dropped that rule on 2026-10-18 and 0.2.47 removed the target and the switch; the CI logs up to
  that date hold the proof. It built with GCC 10.3.1 from
  `tools/fetch_gcc10.sh`. That script downloads the jammy `.deb`s over https, checks each against a
  pinned sha256, and unpacks them into `~/opt/gcc10` without installing them: binutils 2.38 and newlib
  3.3 headers. The build uses Atlan's `build_time.h`
  (2026-02-13 18:59:33), and the result `cmp`s **identical** to
  Atlan's `fnirsi_1013d_scope.bin`. `make package` with that build was also identical
  to Atlan's 721,992 B SD package. The source tree and flags are therefore exactly what Atlan
  shipped.
- **apt GCC 13.2.1:** the build succeeds but the code differs. With the same timestamp, about
  314 k of 346 k common bytes differ because everything after the first change shifts.

| | `.text` | `.data` | `.bss` | image |
|---|---|---|---|---|
| Atlan (GCC 10.3.1) | 0x3ADB0 | 0x18210 | 0x52A748 | 346,112 B |
| here (GCC 13.2.1) | 0x3B608 | 0x18210 | 0x52A748 | 348,672 B |

The memory layout matches the linker script. `_start` and the 32-byte eGON header are at
0x7FFFFFE0 and code starts at 0x80000000. The sections are the same in both builds: `.text`,
`.rodata.str1.4`, `.ARM.exidx`, `.text.startup`, `.data`, then `.bss` up to about 0x8057F738,
below the stacks at 0x81FB5000-0x82000000 (`start.s`). The GCC 13 image is untested on hardware.
The bench and release image is the GCC 10 build: `make gcc10`, then `build-gcc10-pc/fnirsi_1013d.bin`.

## 2. Image format

Every image starts with a 32-byte eGON header (`start.s`):

| offset | field |
|---|---|
| 0x00 | `0xEA000006`, a branch over the header |
| 0x04 | magic `eGON.BT0` (BROM SPL) or `eGON.EXE` (loaded by the bootloader) |
| 0x0C | checksum |
| 0x10 | length in bytes, header included, a multiple of 512 |
| 0x14 | `SPL\x02` or `EXEC` |

`mksunxi` pads the file to 512 bytes, rounds the length field up to 512, and stores the checksum.
The checksum is the sum of all u32 LE words over `length` bytes, computed with 0x5F0A6C39 in the
checksum field. `tools/mksunxi.py` was verified byte-identical against the binary mksunxi
(md5 d900017a..., identical in all pecostm32/Atlan dirs) on both scope builds and on a set of
probe inputs. Only the BROM checks the checksum, for `eGON.BT0` at sector 16. The bootloader
checks only the `eGON.EXE` magic and the length, then copies the image from offset 32 to
0x80000000 and jumps there. The scope is linked at 0x7FFFFFE0 for that reason.

`flashfilepacker -o OUT -i IN -l LOC` works as follows (source:
`FNIRSI-1013D-1014D-Hack/Test code/flashfilepacker/flashfilepacker.c`):

1. It takes OUT truncated or zero-filled to LOC.
2. It appends IN.
3. It appends 72 bytes: 36 zero bytes, then 9 big-endian check words. Word 0 is the byte sum.
   Words 1 to 8 are, for bit 7 down to bit 0, the sum of the indexes of bytes with that bit set.

The stock FNIRSI updater verifies those check words. The SD bootloaders ignore them. Atlan's
package ends with this trailer. `make_sd_image.py pack` is byte-identical to the binary,
verified on the scope image and on random data with a gap.

## 3. SD card layout (Atlan4 boot chain, BOOT fw v0.8)

512-byte sectors, raw card addressing:

| sector | content | limit | written by |
|---|---|---|---|
| 0 | MBR. The FAT32 partition must start at sector 16384 (8 MiB) or later (fw 0.2.101; 2048 before). | | |
| 16 | bootloader `eGON.BT0` (Atlan "BOOT fw v0.8" in the v1.00o5 package) | 32 KiB, sectors 16-79 | BROM loads it to SRAM 0x0 |
| 80 | updater `eGON.EXE` (v0.02v in the package) | up to sector 707 | boot mode 3 |
| 708 | calibration data | 1 sector | scope / updater |
| 709 | scope settings | 1 sector | scope |
| 710 | display + touch config, boot-mode byte | 1 sector | loader tool / scope |
| 750 | scope `eGON.EXE` (`build/fnirsi_1013d.bin`) | must end before the partition and below 16384 (the firmware's write window); 0.2.101 is 774,144 B = 1512 sectors, ends at 2261 | boot mode 0 |

Sector 710 is loaded to DRAM 0x81BFFC00 by the bootloader:

| bytes | content |
|---|---|
| 0-27 | display timing: `AAAAAAAA 55555555 timing1 timing2 CCCCCCCC 33333333 sum6` |
| 28, 29 | touch X/Y swap: 0x00 = swap, 0xFF = none |
| 31 | **boot mode**, DRAM 0x81BFFC1F, byte 363551 of the card |

| boot mode | action |
|---|---|
| 0 | open firmware from sector 750 |
| 1 | stock FNIRSI firmware from SPI flash 0x27000 |
| 2 | FEL: jump to the BROM at 0xFFFF0020 with DRAM, display and clocks already set up |
| 3 | updater from sector 80 |
| >3 | touch menu with PECO / FEL / FNIRSI / Update buttons. The value & 3 is preselected. The timer runs 2 s by default, and a touch extends it. |

Sources:

- `research/atlan4-extracted/updater 0.02o .../fnirsi_1013d_updater/update.h:21-27` defines
  sectors 16, 80, 750, 708, 709 and 710.
- `research/atlan4-extracted/updater 0.02l/.../fnirsi_1013d_startup/fnirsi_1013d_startup.c` is
  the bootloader v0.7 source, the newest available. Lines 32-36 hold the sector and boot-byte
  defines. Lines 138-142 and 295-344 select the image. Lines 350-400 are `load_from_SDcard`:
  `eGON.EXE` check, length at +16, copy from +32 to 0x80000000.
- v0.8 is binary-only. It sits in Atlan's package at sector 16, and its strings match the v0.7
  menu. Its `fpga_check_ready` loop, waiting for 0x1432 at offset 0x1340, is **not referenced**
  anywhere in v0.8. v0.7 calls it, and v0.7 hangs on a custom FPGA bitstream (0x1532). This is
  an inference from disassembly, and it fits the "bootloader v0.8 or higher" note in the FPGA
  readmes.
- The `updater 0.02o` copy of `load_from_SDcard` is never called, because its call is commented
  out. The updater only writes files to these sectors (`statemachine.c:140-264`).
- **No size limit in the boot path** (fw 0.2.101 check, firmware/AGENTS.md section 67). v0.7's
  `load_from_SDcard` (`fnirsi_1013d_startup.c:348-400`) takes the length from the eGON header,
  reads `(length + 511) / 512 - 1` sectors in one `sd_card_read` to 0x800001E0, and checks only
  that they are on the card (`bl_sd_card_interface.c:311-381`). v0.8's copy (package offset
  0x53A0, called with 750 = 0x2EE for boot mode 0) is the same code by disassembly. The data
  phase's time-out grows with the transfer (`sd_send_data`, `bl_sd_card_interface.c:1082-1098`;
  v0.8 offset 0x2C0C). The only bound is DRAM: the bootloader keeps sector 710 at 0x81BFFC00 and
  its variables above 0x81C00000, about 28 MiB above the image's base, and our linker script stops
  image + .bss at 0x81A00000. The firmware's window ends at 16384 (8 MiB) far below both.
- The updater's "scope.bin to sector 750" (`update.c:559-700`) writes the file sector by sector with
  no size or partition check, so on a card partitioned below the image's end it overwrites the
  start of the FAT partition. The partition rule (16384 or later) protects that path too.

**Warning about the upstream `dd` instructions.** `dd if=fnirsi_1013d.bin of=/dev/sdX bs=1024
seek=8` writes the whole package from sector 16 through the scope image's end (Atlan's own
package ends at 1425, ours at 2261 for fw 0.2.101). Atlan's package has zeros at
708-710, so this command wipes calibration, settings and the display/boot-mode sector. Use
`make_sd_image.py write`, which writes only the parts you pass. A GPT card cannot be used,
because the GPT entries occupy sectors 2-33.

### make_sd_image.py

```
python3 tools/make_sd_image.py info  card.img                 # or an SD package / card dump
python3 tools/make_sd_image.py extract fnirsi_1013d-fw-X.Y.Z-sdpackage.bin -d parts/
python3 tools/make_sd_image.py write --image card.img --scope scope/build/fnirsi_1013d.bin
python3 tools/make_sd_image.py write --image card.img --bootloader parts/bootloader.bin \
        --updater parts/updater.bin --scope scope/build/fnirsi_1013d.bin --boot-mode 4
python3 tools/make_sd_image.py pack -o pkg.bin -i scope.bin -l 0x5BC00   # = flashfilepacker
```

`write` behaves as follows:

- It checks each part's magic, mksunxi checksum and region size (the scope image must end below
  16384), and that no part runs into an MBR partition. It never writes the MBR: a card whose
  partition starts too low is refused with the start it needs and the steps to recreate the
  partition at 8 MiB. A partition below 16384 that still fits gets a note.
- It refuses GPT cards, and refuses device-looking paths passed to `--image`.
- It sets only byte 31 of sector 710 for `--boot-mode`.
- It reads every write back to verify it.
- `--create` makes a new zero-filled image, 8 MiB (sectors 0-16383) by default.
- A physical card needs `--device /dev/sdX`, or `\\.\PhysicalDriveN` on Windows (untested). The
  card must have an MBR. You must type the device path to confirm. Sectors 0 to the last written
  sector are always backed up to `backups/` before writing.

Take and verify a full card backup before any of this, as AGENTS.md requires. During
development, boot mode 4 (menu, preselect scope) is the safest choice. A crashing scope image can
then still be bypassed at power-on through FEL or FNIRSI.

Tested here only on image files: a synthetic 4 MiB card with MBR, plus Atlan's package. No
physical card has been written.

## 4. FEL loading (RAM only, SD untouched)

FEL is the BROM USB loader, USB ID `1f3a:efe8`. `sunxi-fel version` should report
`soc=00001663(F1C100s)` (`research/notes/web-sources.md`). FEL itself does not set up the
scope's DRAM, so the scope image must be loaded after something else has initialised it.

**A. Through Atlan's bootloader, the normal path.** Set boot mode 2, or pick "Start FEL mode" in
the menu (boot mode >3). The bootloader initialises clocks, DRAM and the display, shows "Running
FEL mode" and jumps to the BROM FEL entry at 0xFFFF0020. Then run:

```
sunxi-fel version
sunxi-fel -p write 0x7FFFFFE0 build/fnirsi_1013d.bin exe 0x80000000     # = make fel
```

The image is linked at 0x7FFFFFE0, so its 32-byte header lands just below DRAM and code starts at
0x80000000. Source: Atlan `dist/.../How_to_load_scope.txt` and the v1.00o5 Makefile post-build.

**B. Bare BROM FEL with pecostm32's DRAM stub.** This path is for when no open bootloader is on
the card, for example with an SD card holding only `fel-sdboot.sunxi` at sector 16
(`Hacked files/sunxi_stuff/`):

```
sunxi-fel -p spl "FNIRSI-1013D-1014D-Hack/Test code/fnirsi_1013d_startup_with_fel/dist/Debug/GNU_ARM-Linux/fnirsi_1013d_startup_with_fel.bin"
sunxi-fel -p write 0x7FFFFFE0 build/fnirsi_1013d.bin exe 0x80000000
```

The stub (`fnirsi_1013d_startup_with_fel.c`) runs these steps, then jumps back to FEL at
0xFFFF0020:

1. clock, DRAM, SPI and display init
2. FPGA init and the boot logo from the stock SPI flash at 0x13000
3. `check_fpga_ready()`

The FPGA check waits for version 0x1432, so this stub hangs with Atlan's AL3 bitstream (0x1532).
It is only usable with the stock FPGA, unless rebuilt.

**xfel** (MIT, github.com/xboot/xfel) is not packaged for Ubuntu 24.04 and would have to be built
from source with libusb-1.0-dev. The equivalent commands are below. Whether xfel's F1C100s DDR
init matches this board (32 MB DDR, 156 MHz, pecostm32 `dram_control.c`) is unverified.

```
xfel version
xfel write 0x7FFFFFE0 build/fnirsi_1013d.bin
xfel exec 0x80000000
```

### Hands-free loop: `fnirsi1013d-flash`

With a protocol firmware running (ENTER_FEL needs 0.1.1 or later), one command reboots the scope into FEL,
loads a new image into RAM and waits for it to come back on the serial port:

```
mcp/.venv/Scripts/fnirsi1013d-flash.exe                                   # default image build-gcc10-pc/fnirsi_1013d.bin
mcp/.venv/Scripts/fnirsi1013d-flash.exe --image firmware/scope/build/fnirsi_1013d.bin --port <port>
mcp/.venv/Scripts/fnirsi1013d-flash.exe --no-fel                          # already in FEL (boot menu)
```

It runs `#VER` and GET_INFO, sends ENTER_FEL (without the capability it asks for the boot menu instead), polls
`usbipd list` for `1f3a:efe8`, attaches it with `usbipd attach --wsl --busid <id>`, runs
`wsl -d <distro> -u root -- sunxi-fel version` and the `-p write 0x7FFFFFE0 <image> exe 0x80000000` load (the
image path converted to `/mnt/d/...`), then waits for 1209:0001 and prints its `#VER`. Each step is printed with
its time; the exit code is non-zero on failure (2 image, 3 scope port, 4 FEL device / usbipd, 5 sunxi-fel,
6 the port did not come back). The FEL device must have been shared once from an admin shell
(`usbipd bind --hardware-id 1f3a:efe8`). On a Linux host the same command calls sunxi-fel directly
(`--sunxi-fel "sudo sunxi-fel"` if the udev rule is missing). Options: `--distro`, `--fel-timeout`,
`--port-timeout`. Only RAM is written; announce the reload at the bench first (AGENTS.md).

### USB from Windows

- **WSL with usbipd-win** (5.3.0 installed on this machine). Run from an admin PowerShell while
  the scope is in FEL:

  ```
  usbipd list                                   # find 1f3a:efe8 and its BUSID
  usbipd bind --busid <BUSID>                   # once
  usbipd attach --wsl --busid <BUSID> --auto-attach
  wsl -d <distro> -u root -- sunxi-fel version
  wsl -d <distro> -u root -- bash -lc "cd /mnt/d/path/to/fnirsi-1013d-mcp/firmware/scope && make fel"
  ```

  Run as root, or use `make fel SUNXI_FEL="sudo sunxi-fel"`, because WSL normally does not apply
  the sunxi-tools udev rule. The device re-enumerates after `exe` or a power cycle, and
  `--auto-attach` re-attaches it.
- **Native Windows:** install the WinUSB driver for `1f3a:efe8` with Zadig, then use a Windows
  build of sunxi-fel or xfel. Do not mix the two approaches on the same device, because a bound
  usbipd device is not visible to Windows tools.

On the maintainer's unit, FEL entry from the boot menu, `sunxi-fel version` through usbipd, and a FEL
RAM load of the v1.00o5 scope image (`sunxi-fel -p write 0x7FFFFFE0 scope.bin exe 0x80000000`)
were verified on 2026-10-18 (`docs/hardware-status.md`). Hardware steps are done by the maintainer.

## 5. PC control protocol (USB CDC)

The scope speaks the binary protocol of `docs/protocol.md` on its USB port. The integration plan is
`docs/plans/firmware-usb-protocol.md`. Deviations from the spec are listed in the "Implementation
notes" section of `docs/protocol.md`.

### One supported FPGA bitstream (fw-one, protocol 1.25)

Every firmware version runs on exactly one FPGA bitstream, the project's latest (owner decision 2026-10-20: "every
firmware version only supports the latest FPGA bitstream ... detect this and show a loud banner"). The constant is
`PC_FW_FPGA_VERSION` in `pc_cmds.h` (0x153B, `fpga/al3/dist/manifest.json` "recommended"); the capability word, the ring
geometry (8192 words, 16382 samples) and every feature decision derive from it, never from the version word the FPGA
reports. That word is read once at boot (`pc_glue_init`) and compared:

- **Match**: the firmware runs as before. The FPGA's own encode self-check (read 0x33 at boot and after every 200 MSa/s
  capture, enc_heal.c) and the capture stages (0x63 / 0x36, cap_stage.c) stay: 0x153B needs them.
- **Mismatch** (older or newer word): the scope boots into a full-screen notice instead of the scope
  (`ui_diag.c ud_build_fpga_mismatch`, `ui_diag_view.c ui_fpga_mismatch_pass`): "This firmware needs FPGA bitstream
  0x153B." / "This scope has 0x1532." / "Flash it as the guide says (docs/guide/prepare.md), or remove the card to run the
  stock firmware.", with three buttons: Diagnostics (the version word, Save report, the log, Enter FEL; Back returns to
  the notice; no Calibrate, DC shift trim or Volts calibration there), Settings and USB drive (the modal screens of the
  main menu; the notice is redrawn after). The acquisition, the Scope, Spectrum and Frequency response views and the
  generator never run; nothing is written to the FPGA's feature registers at boot. Over USB: GET_INFO 76 carries the
  built-for word and 78 bit 0 the mismatch (GET_HEALTH 124 / 126 the same), every command outside the basic set answers
  E_UNSUPPORTED detail 1 "FPGA bitstream mismatch: this firmware needs 0x153B", and PING, GET_INFO, GET_STATUS,
  GET_HEALTH, GET_LINK_STATS, GET_EVENT_LOG, GET_CAL, the SD commands, SAVE_REPORT, READ_SCREEN and ENTER_FEL keep
  working (`F_BASIC` in pc_cmds.c), so a host can read the version word, save the report and load another image.

Gone with it (fw-one, after fw 0.2.102): the stock 0x1432 / Atlan 0x1532 / EF2 / EP4 arms (`pc_variant_from_version`,
`fpgasettings.fw_FPGA`, `fpga_check_ready`), the 0x1532..0x1537 encode-heal and core-order workarounds (`eh_mode`,
`eh_affected`, `eh_order_differs`, the 0x0D re-write before a re-arm, `PC_ENCF_ORDER`), the fw 0.2.97 stop geometry and the
"may be spliced" flag (`pc_stop_kind`, `pc_auto_stop_word`, `pc_auto_cut`, `pc_engine_record`, `pc_hal_force_stop`), the
per-bitstream bus contract (`fpga_bus_contract_for`; `FPGA_READ_DUMMY` is the one constant), `cs_has`, and every branch on
the version word or on a capability bit that cannot be false on 0x153B (62 sites in 11 files). A new bitstream means a new
firmware: change `PC_FW_FPGA_VERSION`, the bus contract constant if the readout changed, and the notice's text follows.
fw-c5 did exactly that for 0x153C: the constant, `PC_FW_FPGA_VERSION_TEXT` (the mismatch error's text) and the caps.

### Trigger types and hold-off (FPGA 0x153C, fw-c5, protocol 1.26)

Bitstream 0x153C (fpga-c1, `fpga/al3/AGENTS.md` "0x153C C1 triggers"; contract `docs/plans/fpga-0x153c.md` section 3) adds
a trigger qualifier in front of the edge compare and gives Atlan4's dead 0x1B register its meaning. Registers: 0x5E type
(bits 2..0: 0 edge, 1 pulse width, 2 Nth edge, 3 timeout; 4 runt and 5 window are 0x153E and act as edge) with bit 3
"longer than N"; 0x5F u24 N; 0x60 u24 H; 0x16 the polarity per type (the UI's trigger edge); 0x1B u32 hold-off in 5 ns
clocks (0 = off: from the trigger of a record no trigger is accepted until it elapsed, across sequencer restarts and
re-arms; 0 written releases a running one at once); 0x66 bit 0 = 1 makes the next 0x01 = 1 clear the hold-off (one-shot);
reads 0x34 (3 bytes: qualifier status, edges still to count) and 0x37 (hold-off running, type bit 2). Units: N and H are
samples of the trigger stream, one memory word on CH1 / CH2 (10 (D + 1) ns legacy, N x 5 ns with the filter), one 5 ns
clock on EXT. The trigger position is the edge type's for the qualifying edge, so 0x14 and the record arithmetic are
unchanged; power-on (every register 0) is 0x153B behaviour.

- **One copy, one writer** (`fpga_bus.c`): `fpga_trig` is what the firmware believes the FPGA holds. `fpga_trig_write`
  (SET_TRIGGER2, SET_TRIGGER's hold-off, boot) writes 0x5E, 0x5F / 0x60 for a type other than edge, 0x1B, and 0x66 = 1
  when asked (a new trigger definition is a new session: the next arm starts with a clean hold-off; ARMs between
  definitions keep it running across re-arms, the FPGA's rule, which is what skips the pulses of a train). Every arm (UI
  conversion or host ARM, `fpga_arm_reset`) re-writes 0x5E and, for a qualified type, 0x5F / 0x60 inside the sampling
  reset window after the second 0x0B / 0x0C and before 0x0E (`fpga_trig_arm`, 2 or 10 strobes): the FPGA holds its
  qualifier in reset until the first write clock edge after the release, so the registers apply at every rate, and a
  FEL-loaded firmware's FPGA holds what the firmware believes. Boot (`pc_glue_init`, built-for word only) writes the
  power-on state (edge, no hold-off) and the clear request once.
- **Host** (`pc_cmds.c`, `docs/protocol.md` row 66): **SET_TRIGGER2 0x0219** (24 bytes: type, polarity, flags, source,
  N, H, level, level2, hold-off, 0; no mask, every field written; the firmware converts nothing) and **GET_TRIGGER**
  52 bytes (12 the hold-off, 28..51 the SET_TRIGGER2 body that reproduces the trigger). Runt / window / range / both
  cores (0x153E) and out-of-range fields are E_BAD_ARG with the byte offset as detail; nothing is written then. The old
  SET_TRIGGER keeps the edge type's fields, the geometry and the mode, and now accepts its `holdoff` (5 ns units).
  GET_STATUS 48..51 carries 0x37 / 0x34 read at every MAIN poll while a host capture is in flight (6 strobes), GET_CAPTURE_INFO
  96..111 the extension the capture was armed with. Caps bits 2 HOLDOFF and 20 TRIG_TYPES, features bit 8.
- **Not here**: the UI's Type row (`docs/plans/ui-rows.md`, a later item, converts the menu's time to samples and
  drives the same `fpga_trig_write`), peak detect (C6) and the counter (C7).
- **Host tests**: `test_fpga_bus.c` `test_trig_ext` (every type's write and arm sequence against `fpga_model.c`, the
  0x66 one-shot, the hold-off masking a trigger across a re-arm, released by 0x66 + arm, by 0x1B = 0 or by running out,
  the status reads), `test_session.c` `test_trigger2` (the commands, the refusals, settings_seq, the status bytes, the
  capture info), the 1.26 vectors in `docs/protocol-vectors.json`.

### Build options

| make variable | default | effect |
|---|---|---|
| `PC_DEBUG` | 0 | 1 adds DEBUG_FPGA and FORCE, sets caps DEBUG_RAW and FORCE_TRIGGER. Bench builds only |
| `PC_FORCE_CDC` | 1 | CDC at boot even if the saved "USB CH340 mode" setting says MSC |
| `PC_FIFO_MAP` | 0 | USB endpoint FIFO layout (`pc_transport.h`). 1 = the fw 0.1.3-0.1.5 layout, fw version gets `-m1` |

```
make                          # GCC 13: build/fnirsi_1013d.bin
make gcc10                    # Atlan's GCC 10.3.1, the bench and release image: build-gcc10-pc/fnirsi_1013d.bin
make PC_DEBUG=1 BUILD=build-dbg
make -C host_tests test       # host gcc: frame layer, vectors, session tests (scope/host_tests)
```

The `PC_PROTOCOL=0` build (Atlan's v1.00o5 unchanged) and `make check-reference` were removed in 0.2.47
(section 1). Warning policy: no new warning under either compiler, in release or `PC_DEBUG` builds (0.2.47:
GCC 10 36, GCC 13 31, all upstream).

Files:

| file | role | host build |
|---|---|---|
| `scope/pc_protocol.c/.h` | frame parser and encoder, CRCs, ASCII probe lines | yes |
| `scope/pc_cmds.c/.h` | dispatcher, handlers, shared control with the UI (protocol 1.3), host single capture | yes, via `pc_hal.h` |
| `scope/pc_hal.h` | the only interface `pc_cmds.c` uses to reach the scope | |
| `scope/pc_hal_scope.c` | `pc_hal.h` on the 1.00o5 settings and `fpga_control.c`, firmware glue | no |
| `scope/pc_transport.c/.h` | USB RX ring, TX pump, ZLP, link events | no |
| `scope/pc_rx_ring.h` | RX ring copies and the one-FIFO-read-per-packet store | yes (`test_rx_ring`) |
| `scope/pc_hooks.h` | the hooks in the 1.00o5 files (macros that mark each place) | |
| `scope/fpga_bus.c/.h` | fw 0.2.72: the bus contract (one dummy strobe, `FPGA_READ_DUMMY`), the readout start, the sampling reset order, the record of rate / filter writes | yes (`test_fpga_bus`, against `host_tests/fpga_model.c`) |
| `scope/build_id.c` | the build identity as data: commit hash and commit time (section 1, "Reproducible images") | |
| `scope/host_tests/` | `test_protocol`, `test_rx_ring`, `test_session` (mock FPGA and UI), `check_vectors.py` | |
| `../docs/protocol-vectors.json` | shared frame vectors (`gen_vectors.py`; the MCP server merges its own) | |

Hooks in the 1.00o5 code (up to 0.2.46 all under `#if PC_PROTOCOL`, since 0.2.47 always built):

- `fnirsi_1013d_scope.c`: `pc_glue_init()` before `usb_device_init()`. The main loop calls
  `pc_poll(MAIN)` first, then the MCP indicator service, then the normal UI pass (protocol 1.3 shares
  control: the UI loop always runs).
- `scope_functions.c`: a BUSY poll in the 12 calibration and auto-setup conversion spin loops. In
  `scope_acquire_trace_data`: the arm condition gains `PC_UI_ARM_ALLOWED()` (false while a READ_RAW
  waits for the capture just finished), the raw 0x14 word is reported (`PC_UI_CAPTURE_ADDR`), and the
  finished readout is counted (`PC_UI_CAPTURE_DONE`).
- `timer.c`: a poll from the 1 ms timer interrupt. It answers PING, GET_INFO, GET_STATUS,
  GET_BATTERY, `#VER` and `#RESET`, and pumps TX, without waiting for the UI loop. Everything else
  stays on the main loop. It never runs while a main-loop or BUSY poll, or the REMOTE service, is
  active.
- `touchpanel.c`: a BUSY poll in `tp_i2c_read_status()`. Every modal menu loop and the MSC screen
  scan the touch panel, so the link answers PING, GET_INFO, GET_STATUS and GET_BATTERY there.
- Every modal loop (fw 0.2.78, protocol 1.23): `PC_UI_YIELD()` after its touch scan. A request that needs the main
  loop while the UI is modal sets the session's yield flag (timer poll or BUSY poll, `pc_cmds.c` `pc_ui_yield`); each
  loop then leaves by its own cancel path (a menu or fold-out closes, a dialog answers Cancel / No, a screen Back /
  Exit / Later, the wizard between steps Cancel) down to the base screen, where the MAIN poll answers the request and
  nothing is shown (fw 0.2.81; 0.2.78-0.2.80 showed a 4 s note). Steps that must finish set a hold reason
  (`pc_glue_ui_hold(PC_HOLD_CAL / _SAVE / _USB_DRIVE)`): the request then gets E_BUSY with it (details 3, 4, 5); a
  UI pass with no yield check that outlasts 1.5 s gives E_BUSY detail 2 (`docs/ui-guidelines.md` section 4,
  `docs/protocol.md` section 14 row 63).
- `fpga_control.c`: a BUSY poll every 65,536 polls of the 0x05 ready wait. The UI wait stays
  unbounded, so UI acquisition behaves exactly as 1.00o5. `fpga_do_conversion` ends with
  `PC_UI_CAPTURE_START()`: the ring no longer holds the last capture, and a host single is aborted.
- `power_and_battery.c`: the `PC_POWER_OFF()` hook is empty since 0.2.0. Host changes are live
  settings and are saved like the user's.
- `usb_interface.c`, `cdc_class.c`: the transport hooks, an endpoint FIFO layout that fits the
  F1C100s's 2 KiB USB FIFO RAM and keeps clear of the fixed EP0 FIFO at 0-63 (`pc_transport.h`; 1.00o5
  put the CDC OUT FIFO on EP0 and the IN FIFO at byte 4096), and the new USB identity
  (VID:PID 0x1209:0x0001 pid.codes test PID, product `FNIRSI 1013D`, serial `1013D-0001`). The
  text debug path is gone, so the debug helpers in `menu.c` print nothing. MSC is unchanged.
- `variables.h`: the trace buffers are rounded up to 4096 words (16,384 bytes per channel).

### Removed and hidden menu options (fw 0.2.6, removal fw 0.2.47)

Maintainer policy: the UI offers only what the running hardware can do, and what the 1013D can never do is removed in
code, not hidden (maintainer, 2026-10-18). Removed in 0.2.47 (settings sector and waveform file slots stay, written 0):
the generator's Wave (SQUR / SINE / TRGL), Ampl, Offset and Phase rows (no DAC: the generator is the FPGA's DDS
square wave on the calibration pin; 1.00o5 never sent them anywhere), the trigger Hold off row (never sent to the
FPGA), Long memory (1.00o5 forced it off; its main-loop branch was empty) and the channel menus' FFT switch
(1.00o5 stored `fftenable` and drew nothing). The menus are compact (maintainer: no gaps; generator 3 rows, 169 px:
Power, Freq, Duty; trigger 5 rows, 280 px; channel menus: Invert, Coupling and V/A one row up, the empty slot above
Probe mode, the box keeps its height for the V/div column); `*_MENU_HEIGHT` in `variables.h` also sizes the
screen save/restore and the touch area. What depends on the running hardware is hidden per item (touches ignored;
`pc_glue_ui_has` / `pc_glue_ui_mask` in `pc_hal_scope.c`, hooks `PC_UI_HAS` / `PC_UI_MASK`):

| item | where | hidden when | why |
|---|---|---|---|
| memory depth 9000, 12000, 15000 | acquisition menu | Filter on | the 16-bit layout holds one sample per ring word: at most 8190 samples |
| (history, fw 0.2.6..0.2.102) the generator menu, the memory depths above the bitstream's maximum, the Filter selector and the EXT trigger source | top bar, acquisition and trigger menus | the running bitstream lacked the feature | since fw-one the firmware runs on one bitstream that has them all; a mismatched scope shows the notice screen instead |
| Time & Date settings (RTC submenu) | system settings | no DS3231 answers at boot | the clock, file stamps and FAT times would read garbage |
| Temperature (Diagnostics Battery tile) | Diagnostics | no DS3231 answers at boot | it is the DS3231's temperature register; without the module it read "99" |

Relabelled (fw 0.2.10): the acquisition menu's "ACQ / Long mode" box (it named the mode a tap switches to) is drawn
as a state toggle, "Roll mode" + "on" / "off", green when the long (roll) time base is on, like Average and Long
memory. It stays: roll mode works on this hardware. System settings (fw 0.2.13): without a DS3231 the Time & Date
row (the last one) is not drawn and the menu box is one row (59 px) shorter; `SYSTEM_MENU_HEIGHT` is a runtime value
in protocol builds (`pc_glue_sys_menu_height`), feeding the box, separators, slide-in and screen save/restore.

Battery indicator (fw 0.2.13): the level is a running average, and while charging it changes only when the average
moves 1.25 steps away from the shown value (the raw level followed ADC noise while charging over USB and the last
column flapped). Off the charger the 1.00o5 rule stays (the level only drops), applied to the average.

Version display: the Diagnostics Status page's identity line (fw 0.2.59; the restore screen until 0.2.56, the top bar
until 0.2.51), from the image's identity record (the same as `#VER` and GET_INFO). Waveform and reference files still store 1.00o5's VERSION_STRING (file format data).

At boot a saved setting that selects a hidden option is corrected: RTC off without a module, EXT trigger -> CH1,
memory depth clamped to the largest offered step. The DS3231 is detected by addressing it (0xD0) on the touch
panel's I2C bus and sampling the ACK bit [U, bench check: with and without a module]. No option needs a manual switch
yet (everything above is detected); new hardware mods that cannot be detected get a row on the Settings screen
(fw 0.2.55, "Settings" above; its two columns are full: a new group needs a second page) with the value in a spare settings word (108..119 or 204..255,
`scope_save_config_data`; 207 holds the Filter setting since 0.2.30, 208-210 the spectrum and phosphor views, 224..255 the unit life record since 0.2.52, so 211..223 are free), default off. Not changed: trigger positions 25 % / 75 % on the stock bitstream (software
only there), the generator top-bar button text on stock.

### Decimation filter (FPGA 0x1534, fw 0.2.30, protocol 1.9)

fpga-gen3's AL3 bitstream 0x1534 filters the 200 MSa/s ADC stream before decimating (fpga/al3/src/FPGA commands.txt,
fpga/al3/README.md): 0x53 mode (K: 0 legacy sample skipping, 1 boxcar, 2 CIC2, 3 CIC3; bit 2 core-order swap;
bits 3 / 4 capture the A / B cores on the falling write-clock edge), 0x54 output shift S, 0x55 16-bit trigger level,
read 0x29. Latency 120 ns pin to memory for every K and N, the sample centred K (N - 1) 2.5 ns earlier still. Decimation N = 2 x (floor(D / 2) + 1) for the 0x0D
divider D, so the odd dividers of `sample_rate_settings` give the same rates as before; the FPGA stores
min(65535, y >> S), y = filter output with gain N^K. On 0x1532/0x1533 none of this is written (caps bit 15 DECIM).

Filter per mode (fw 0.2.64, maintainer): the Scope view defaults to CIC2 without the FIR (the FIR's 6-9 % step overshoot
pulses on a square wave), the Spectrum view to CIC2+FIR. Each mode keeps its own setting (`pc_dec_set_mode[2]`,
`PC_DECIM_DEFAULT_SCOPE` / `_SPEC` in pc_decim.h): settings word 207 the Scope's, word 212 (0xD500 marker) the
Spectrum's. The Filter box, the arm and SET_DECIM / GET_DECIM act on the mode on screen. A 0.2.30..0.2.63 sector with
the old single default (CIC2+FIR) loads as the new defaults; any other saved choice is kept for both modes.

- Maths, `pc_decim.c` (integer, host-tested): S = max(0, bitlen(255 x N^K) - 16), so full scale uses the top bit of
  16; exactness 255 x N^K < 2^48 makes CIC3 fall back to CIC2 above N = 10334 (slower than 20 kSa/s) and CIC2 to
  boxcar above N = 1,050,630 (not reached by the table); legacy at divider 0 (200 MSa/s: K >= 1 would give N = 2)
  and for captures over 8190 samples. 16-bit trigger level L16 = round(v x N^K / 2^S) from the UI's 8-bit level.
- When it is written (`pc_hal_scope.c`, "Decimation filter"): at every conversion start. `fpga_do_conversion` writes
  the Filter setting for the UI acquisition (`scope_acquire_trace_data` flags it) and legacy (K = 0) for every
  other caller, i.e. calibration (it needs the per-core averages) and auto setup; host ARM writes it in
  `pc_hal_arm`. Only changed registers are written (0x55 always); a change of mode, shift or divider (tracked in
  `fpga_set_sample_rate` and the raw SET_TIMEBASE path) waits (K + 2) output periods, rounded up to whole ms + 1,
  before the sampling reset (the filter runs free and 0x01 does not reset it; at most 9 ms at 500 Sa/s).
- Geometry: in the 16-bit layout 0x0B / 0x0C / 0x0E / 0x14 count samples (one word each), so `fpga_do_conversion`
  writes `settriggerpoint` and `totalsamples` unhalved and the UI capture start is (0x14 - pretrigger) mod 8192
  (`pc_glue_decim_ui_start`) instead of 1.00o5's `T + 7441`.
- Readout: `fpga_read_sample_data` calls `pc_glue_decim_read`, which reads 2 x nofsamples words (0x1F, n x low
  byte 0x20/0x22, 0x1F, n x high byte 0x21/0x23; `fpga_read_decim_data`, 1.00o5 convention without a dummy read)
  into a uint16 buffer per channel, converts them into the 8-bit trace domain (code = out x 2^S / N^K + (comp A +
  comp B) / 2, Q24, rounded and clamped) and lets `fpga_read_adc_data` take those values instead of bus reads in
  its two passes (odd, then even samples, each with its core's compensation pre-subtracted). Trace, measurements,
  frequency and the software trigger therefore run unchanged; the 16-bit buffer stays available to READ_TRACE WIDE.
  Inference: the 1.00o5 rail-matching branch (`checkfirstadc`) still acts within |compensation| codes of 0 and 255,
  and ADC overload now means "the average hit a rail".
- EXT trigger with K >= 1: the data lag the edge by 120 ns + K (N - 1) 2.5 ns, rounded to the nearest output
  sample (`pc_decim_ext_shift`); the UI (`pc_glue_decim_pre_words`) and host ARM write that many fewer pretrigger
  words, so the edge stays at sample `settriggerpoint`; GET_CAPTURE_INFO byte 70 reports the shift.
- Hidden settings: falling-edge capture of the A / B cores (0x53 bits 3 / 4), only via SET_DECIM flags bits 1 / 2,
  saved in bits 3 / 4 of settings word 207; the core-order swap (bit 2) is host only and not saved.
- UI: acquisition menu "Filter" box (off / boxcar / CIC2 / CIC3, a tap steps through them, green while on) in the
  slot of 1.00o5's Long memory box, only on 0x1534+. Saved in settings word 207 (0xDE00 | K); sectors without
  a valid word (new settings, written before 0.2.30) and the DEFAULT button give the defaults (pc_decim.h; CIC2
  since fw 0.2.44, CIC2+FIR since 0.2.48 where the FPGA has the FIR; until 0.2.43 off; since 0.2.64 per mode, below). A saved word is kept as it is. Turning it on with 9000+ samples sets the memory depth to 6000 (trigger point kept at 25 / 50 / 75 %).
  The box shows the setting; GET_DECIM tells the effective K (e.g. legacy at 200 MSa/s).
- Protocol 1.9 (docs/protocol.md section 14 row 47): GET_DECIM 0x020C, SET_DECIM 0x020D (shared control: it is the
  UI setting; SET_TRIGGER total > 8190 is refused while it is on), READ_TRACE flag bit 2 (u16 samples),
  READ_RAW layout 2 (u16 little-endian words), GET_CAPTURE_INFO 76 bytes with the capture's mode, S and N.
- Host tests: `test_session.c` `test_decim_math` (all table rates x K, exactness limits against 128-bit references,
  L16 round trip for every level, compensation, rates, settle) and `test_decim_protocol` (0x1533 untouched, 0x1534
  flow); `decim_check.c` reproduces the 80 "decim" cases that `gen_vectors.py` computes in Python.

CIC compensation FIR (FPGA 0x1535, fw 0.2.44, protocol 1.11, row 49):
- 0x1535 adds a 15-tap FIR after the CIC (fpga-gen4, fpga/al3/README.md): with 0x56 = 1, K >= 1, N >= 16 and N % 4 == 0
  (10 MSa/s and slower in the table) the CIC decimates by N/2 and the FIR by 2, so the output rate is still 200 MSa/s / N.
  `pc_decim_config` takes the capability level (`PC_DECIM_HAS_FIR` on 0x1535+) and the request bit `PC_DECIM_SET_FIR`
  and computes `fir_en` (what 0x56 gets) and `fir` (active); with `fir` the gain is (N/2)^K, S gets + 1 (headroom for the
  6-9 % step overshoot), exactness and the CIC3 limit apply to N/2, the EXT shift uses 250 ns + 17.5 N ns + 1.25 K (N - 2) ns
  and the settle time (K + 10) N x 5 ns + 1 us. Everything downstream (scale_q24, L16, the readout's corr16, the spectrum's
  16-bit path, GET_CAPTURE_INFO) takes gain / S / scale from the configuration, so no other code knows about the FIR.
- `pc_dec_apply` writes 0x56 only on 0x1535+ and only when the enable changes; a change counts as a filter change (settle).
- UI: the Filter box steps off, boxcar, boxcar+FIR, CIC2, CIC2+FIR, CIC3, CIC3+FIR on 0x1535+ (0x1534: no FIR steps). The
  box shows the setting; at 20 MSa/s and faster the FIR is not eligible and the record is plain CIC (GET_DECIM byte 2 bit 5
  clear; the spectrum header tag says "CIC2", not "CIC2+FIR"). Saved in settings word 207 bit 5 (valid mask 0xFFC4).
- Host: SET_DECIM flags bit 3, GET_DECIM byte 1 bit 3 and 0x29 bits 6 / 5 in bytes 2 / 24 / 26, caps bit 16.
- Host tests: `test_decim_fir_math` (every table divider and D 1..200 x K: eligibility by both the N and the D rule, gain,
  S, level round trip, headroom against the 1.21 FIR gain, latency, settle, CIC3 limit at N/2) and `test_decim_fir_protocol`;
  the `decim` vectors have `fir_req` cases (gen_vectors.py, cross-checked by decim_check).
- Bench: GET_DECIM bytes 26-27 = 0xE2 / 0x01 after an arm at 10 MSa/s CIC2+FIR (0xC2 at 20 MSa/s); a DC level reads the
  same code with FIR on and off; a tone at fo/4 is flat with the FIR (CIC2 droop 1.8 dB without); the 100 kHz alias of a
  900 kHz square at 1 MSa/s drops by about 20 dB; EXT edge at sample `pretrigger` with the FIR shift.

Bench (nothing verified on hardware; bitstream not yet flashed): 0x29 read-back after an arm (GET_DECIM bytes
26-27); a DC level reads the same code with the filter off and on; a 3.9 MHz generator at 1 MSa/s aliases at
100 kHz with skipping and is attenuated with boxcar / CIC (plan F1); trigger on CH1 with the 16-bit level at
several rates; the swap bit (T4 dependency); settling after a time base change (first capture clean).

### Segmented capture and hardware averaging (FPGA 0x1536, fw 0.2.58, protocol 1.17)

fpga-gen5's AL3 bitstream 0x1536 adds a sequencer in front of the unchanged 0x1535 capture logic (fpga/al3/README.md
section 6, docs/plans/segmented-capture.md): **segment mode** restarts the capture after each record into the next
slot of 2^m words and logs a 40-bit timestamp and the trigger offset per segment; **averaging mode** adds each
triggered record (pretrigger included) into a 2048 x 64 accumulator. Writes 0x57-0x5D, reads 0x2A (status) and
0x2B-0x32 (bytes of the view word at the 0x1F pointer). On 0x1532-0x1535 none of this is written (caps bit 17 SEQ).

- Files: `pc_seq.c/.h` (pure maths, host-tested: fit rules, register bytes, status and table layouts, slot
  addresses, the UI averaging plan, the exact rounded mean), `pc_seq_hw.c/.h` (target: the pc_hal.h sequencer
  functions and the UI averaging), handlers in `pc_cmds.c` ("Sequencer"). Hooks: `PC_SEQ_PRE_RESET()` in
  `fpga_do_conversion` before 0x01 = 1, `PC_SW_AVERAGE()` in the two software-average conditions of
  `scope_display_trace_data`; glue calls in `pc_glue_decim_conversion`, `pc_glue_decim_read`, `pc_glue_ui_capture_addr`,
  `pc_glue_ui_capture`, `pc_glue_poll_main`, `pc_hal_trace_build`, `pc_hal_arm`.
- Arm order on 0x1536: 0x0B / 0x0C and the sequencer registers are written **before** 0x01 = 1 (and 0x0B / 0x0C again in
  the reset window as before). Reason (fpga-gen6, RTL reading): `trigger_read_address` and the slot mask are
  write-clock registers that load only on write-clock edges while 0x01 = 1; at slow legacy rates (2 us per write clock
  at 1 MSa/s) the reset window can pass without an edge. The 0x05 poll guarantees edges in reset. 0x57 = 0 is written
  for every capture that is not a run while the FPGA may hold another mode (always the first arm after boot: an MCU
  restart leaves the FPGA running). Bitstreams before 0x1536 keep the 0.2.51 order byte for byte.
- **UI averaging: one behaviour (fw-one; owner 2026-10-20 "consistency matters").** The acquisition menu's Average
  (1.00o5: auto or normal trigger mode, not the waveform viewer, not roll) used to average every display pass, trace =
  (trace + previous) / 2, whether a new record came or not; fw 0.2.58 replaced that by a hardware run for legacy
  records that fit the accumulator and kept the running average for the rest. Since fw-one every displayed record is the
  mean of N records, N from one planner for both paths (`pc_seq_ui_plan_n`, pc_seq.c): the largest power of two in 2..64
  whose N records fit the 40 ms budget, a record costed at its acquisition time plus the sequencer's dead time and, on
  the MCU path, its readout (4 bus bytes per ring word at 0.3 us). A legacy record (filter off, or 200 MSa/s) that fits
  the accumulator (total <= 2046 words: the 3000-sample depth = 1500 words) is one hardware averaging run of N records
  (64 at 200 MSa/s, 8 at 1 MSa/s); the readout takes the four lane sums of the channel (S = 0, 4 bus passes of 1500
  words, about 1.8 ms) and feeds `fpga_read_adc_data` the corrected code of the mean per core (`pc_seq_avg_code`: mean
  in Q8 by an exact reciprocal, then the core's gain / offset, rounded). Every other record (16-bit, or deeper than 4092
  samples) is averaged by the MCU (`pc_seq_hw.c` `ui_mcu_collect`): the main loop's conversion is record 1; once it is
  done and triggered, `pc_seq_ui_done` arms the same conversion N - 1 times within the pass, reads each record's lanes
  (`pc_hal_read_core` / `fpga_read_decim_data`) into 32-bit sums per sample and feeds the exact mean (legacy per core as
  above; 16-bit the rounded mean through the readout's scale / corr16 conversion, as a single record). So trace,
  measurements, software trigger, spectrum and phosphor take the mean as a normal record on either path. Numbers: 3000
  16-bit samples at 1 MSa/s N = 4 (3 ms record + 3.6 ms readout each, about 27 ms per pass), 6000 legacy samples at 200
  MSa/s N = 8, 16382 legacy N = 4, 10 kSa/s and slower N = 1 (single records: two do not fit the budget). GET_STATUS byte
  46 tells the path (0 off, 1 the MCU's mean, 2 hardware), byte 47 log2 N (0 = single records). READ_RAW of a UI capture
  reads the last of the N records (the ring), READ_TRACE the mean.
- Deadline, both paths: a hardware run not done after max(100 ms, two records) + its run time is stopped
  (`pc_seq_ui_service`, main loop: 0x5D = 1, then 0x1A = 0) once it holds a record, and shows the mean of what it has.
  Auto mode without any trigger: the run is stopped empty, the record shown is the sample memory's (untriggered, as the
  firmware auto mode of 0.2.41 shows), and the next UI conversions are single records until one triggers. Normal mode
  with no record yet keeps waiting, as a normal-mode capture does. On the MCU path record 1 is the main loop's
  conversion with exactly those semantics (the auto time-out ends it untriggered: shown alone, the next conversion tries
  again; normal mode waits); records 2..N wait at most the same deadline from record 1's arrival, then 0x1A = 0 discards
  the partial record and the pass shows the mean of the records it holds. The pass never stalls longer than the
  planned run time plus that deadline. The firmware auto time-out never runs during a UI run (it would end a record
  between the records of a run).
- Host (protocol row 56): SET_SEQ sets a run for the next host ARM (segments: m, count, circular, hold-off; averaging:
  positions, N, hold-off), checked against the trigger geometry at SET_SEQ and at ARM; DONE is the end of the run;
  STOP ends a run as DONE (0x5D, 0x1A = 0) and keeps its data; GET_SEQ reports the setting, its fit, the FPGA status
  and the latest run; READ_SEQ returns the table (u64 entries) or the averaged record (lane sums >> S, or u32 sums for
  16-bit records); READ_RAW addr_mode 3 reads segment s relative to its record start, wrapping at the slot end. A segment
  run builds no trace (READ_TRACE E_STATE 5); an averaging run's last record is a normal capture in the ring.
- Memory (GCC 10, against main at 0.2.55): .text +11,888 bytes, .bss +8,228 (`ui_lane` 8 KiB), image 536,576 -> 548,352 bytes.
- Host tests: `test_seq.c` (fit limits from the README, every m, register bytes, entry bit positions, slot wrap, the
  plan at the table rates, `pc_seq_udiv` exact for every c 1..256 at all multiples below 2^24, `pc_seq_avg_code` equal
  to the per-sample correction for constant records), `test_session.c` `test_seq_older_bitstreams` / `test_seq_segments`
  / `test_seq_average` against a mock sequencer that applies the FPGA's view rules (shift, clamp, raw), `seq_check.c`
  for the `seq` vector section.
- Not host-testable (target only, read against zaklad.v): the bus sequences in `pc_seq_hw.c`, the UI run and its
  deadline. Bench plan: firmware/AGENTS.md section 33.

### Full-rate capture check (FPGA 0x1538, fw 0.2.62, protocol 1.19)

Why: on AL3 0x1532..0x1536 a 0x0D or 0x53 write can leave one ADC chip's encode clocks half a word out of phase with
the RAM write clock; at 200 MSa/s one core of that chip is then captured while its outputs change, until the next such
write (fpga/al3/README.md "Findings" item 3). 0x1537 made that deterministic but left chip 2's phase to a marginal flop
that is re-resolved at every sampling reset: on the bench CH2 core A was garbled in about 40 % of host arms with no
register write in between (docs/hardware-status.md, 2026-10-19 16:48). 0.2.61 checked only after 0x0D writes and
missed that. 0x1538 (fpga-gen7) drives the encodes from the write clock at every rate (encB = the write clock, encA
its complement, core B the earlier sample: `PC_INTERLEAVE_ORDER` 0, now fixed) and checks its own pins.

What the firmware does (`enc_heal.c/.h` pure, `enc_heal_hw.c` target, `pc_acq_service` in pc_cmds.c for host ARM):
- Every capture at divider 0 (200 MSa/s; always the legacy 8-bit layout) is checked before anything reads it:
  the UI readout, the calibration wizard, the v4 class probe, the 1.00o5 calibration and auto setup (all through
  `fpga_done_conversion`) and host ARM (`pc_acq_service`, before READ_RAW / READ_TRACE can see it).
- 0x1538..0x153F: FPGA read 0x33; bit 0 set means an encode check failed since the last 0x01 = 1. One bus byte.
- 0x1532..0x1537: the residual of 0.2.61 on the record's first 250 words per channel, read from the ring the readout
  reads anyway (1000 bus bytes, about 0.3 ms, no extra conversion): per channel the RMS of core B against the mean of
  its two core A neighbours; above 8 codes the capture is bad (bench: 1.3 codes clean, 14-34 garbled).
- A bad capture is discarded and the same conversion re-armed (on 0x1532..0x1536 after 0x0D = 1, 0, since there only a
  rate write re-rolls the phase), at most `EH_MAX_TRIES` (16) times; then the last one is accepted and flagged. The UI
  keeps showing the previous trace meanwhile; in normal or single mode a re-arm waits for the next trigger.
- Not checked: other rates, stock / EF2 / EP4 bitstreams, and 0x1536 / 0x1537 sequencer runs (segments and lane sums
  are not samples; 0x1538's flag covers a whole run).
- At boot on 0x1538 the firmware reads 0x33 once and requires bits 3:2 = 01 (convention 1); otherwise GET_HEALTH 90
  bit 1, event 21 reason 3 and an amber Diagnostics line. Since 0.2.72 that bitstream's captures are then checked by
  their data (GET_HEALTH 88 = 1), and GET_INFO caps bit 12 (core order unverified) is clear only when the convention was
  read as 1.
- 0.2.72: a 0x0D / 0x53 / 0x56 write after a capture's sampling reset (the time base changed while the capture ran)
  leaves 0x33 bit 0 set until the next reset, so on 0x1538+ that capture is judged by its data instead (`fpga_bus.h`
  record, `eh_capture_mode`; GET_LINK_STATS 67). The 0.2.62 bench re-took 112 of 5798 captures for that reason.
- Reporting: GET_LINK_STATS 62 (captures found bad) and 63 (recovered by a re-arm); GET_HEALTH 88..103 (mode, last
  0x33, flags, 0x33 at boot, bad, recovered, checked); event 21 per capture that needed re-arms (a = reason << 5 |
  re-arms, bit 7 not recovered; b = caller << 12 | first value); the report file's "Full-rate capture check" line;
  Diagnostics FPGA line: 0x1532..0x1536 amber "ADC capture timing race, fixed in 0x1538", 0x1537 amber "ADC core order
  differs: recalibrate after flashing 0x1538", 0x1538 "encode check OK" (", N captures re-taken"), or amber "encode
  check failed on CH2 A, ..." with the pins 0x33 bits 7:4 report wrong. Protocol: docs/protocol.md row 58.
- Cost: on 0x1538 one bus byte per full-rate capture; on 0x1532..0x1537 about 0.3 ms per full-rate capture plus the
  re-arms (0x1537: about 0.67 re-arms per capture on average). The 0.2.61 check after each rate write, which took one
  extra conversion, is gone.
- Host tests: `test_enc_heal.c` (modes, residual, both checks against a mock FPGA, the re-arm bound, 0x1537's 40 %
  statistics, event fields), `test_session.c` `test_enc_check` (0x33 good / bad sequences through ARM / WAIT, the bound,
  GET_HEALTH, a failed re-arm, 0x1537 and 0x1535 data checks, stock unchecked), `test_ui_diag.c` (the FPGA lines),
  `test_diag.c` (report line, event 21 text). The UI path (`fpga_encode_after_capture`) is target-only.

### FFT core (`fx_fft.c`, protocol builds)

Fixed-point real-input FFT for the spectrum analyser (plan: `docs/plans/dpos350p-features.md` 1.2). No libc, no float:
int32 data, Q30 twiddles and windows, int64 products. N = 256..16384 (`log2n` 8..14). An N/2-point complex radix-2 DIT
FFT of x[2n] + i x[2n+1], then the real split step. The input is scaled so that N x max|x| <= 2^29, which bounds every
intermediate value below 2^31 (derivation in `fx_fft.c`): no per-stage scaling and no overflow for any input, 8-bit or
16-bit (16-bit keeps every bit up to N = 16384). Windows: rect, Hann, flat-top (Matlab flattopwin, 5 terms), periodic,
computed at plan time from the build-time cosine table, together with a W_N^k twiddle table (plan memory 2N words,
work buffer N + 2, both 8-byte aligned; 16384 points: 128 KB + 64 KB). Levels come out in centi-dB relative to 1
input unit RMS for a sine centred on a bin (a full-scale 8-bit sine, amplitude 127, reads 39.07 dB on every window),
plus a caller offset (the spectrum view passes 20 log10 volts per code, so the levels are dBV); DC and Nyquist are
corrected for their single bin. dB via CLZ + a 257-entry log2 table with interpolation. ENBW: rect 1.00, Hann 1.50,
flat-top 3.77 bins (`fx_enbw_milli`).

**Speed.** Before fw 0.2.48 the MMU and so the D-cache were off, so every data access, the stack included, went to DDR:
about 90 CPU cycles each on the bench (0.2.27 FFT_BENCH: 4096 points in 24.3 ms, 4x the estimate; the C loops spilled
to the stack and GCC 10 never paired accesses). Since 0.2.31 the hot loops are ARM assembly inside `fx_fft.c`,
everything in registers: the u8 load with window and bit reversal fused with stage 1 (two LDRH, two LDRD, one 4-word
STM per point pair; the window drops to Q22 there so the product fits 32 bits, error below 2^-22), the stages (per
twiddle where blocks outnumber twiddles: 2 LDRD + 2 STRD per butterfly; per block otherwise: + 1 LDRD twiddle), and
the split step (2 + 1 LDRD, 2 STRD). Each has a C twin computing the same integers, which the host build runs.
Measured on 0.2.31: 4096 points 10.9 ms (was 24.3), 2048 points 4.9 ms in DDR, 2.3 ms in SRAM. Since 0.2.37 the dB
conversion between DC and Nyquist is assembly too: one LDRD per bin, the log2 table as (t, delta) pairs (one LDRD),
the window norm, shift and dBV offset folded into one 64-bit constant, so 3 accesses per bin instead of about 9
(byte-identical to the C twin, including the clamps; DC and Nyquist stay in C).

**With the MMU and D-cache on (fw 0.2.48, `pc_mmu.c`).** FFT_BENCH on the maintainer's unit, 2026-10-19 08:10 (CP15
control word 0x0005107D: MMU, D-cache and I-cache on; `docs/hardware-status.md`), MMU off -> on, buffers in DDR:

| Points | MMU off | MMU on |
|---|---|---|
| 1024 | 2.20 ms | 0.21 ms |
| 2048 | 4.95 ms | 0.68 ms |
| 4096 | 10.96 ms | 2.72 ms |
| 16384 | 51.9 ms | 33.7 ms (the working set exceeds the 16 KB cache) |

The same run with the buffers in on-chip SRAM: 1024 points 1.03 -> 0.22 ms, 2048 points 2.30 -> 0.66 ms. DDR and SRAM
are now equally fast, so the SRAM A placement no longer buys speed. The assembly loops are unchanged; the gain is
the cache. The figures above for 0.2.27 and 0.2.31 are history.

Tables: `fx_fft_tables.c` is generated by `firmware/tools/gen_fft_tables.py` and committed (16 KB cosine quarter wave,
log2 table, flat-top coefficients); the host test fails if it differs from the generator.

Host tests (`make -C host_tests test`): `test_fft.py` runs the C build natively (`fft_cli`) against numpy float64 for
every N and window, on noise, bin-centred and off-bin tones with a -60 dB second tone, full-scale DC, alternating full
scale, an impulse, +/-1 LSB noise, 16-bit tones and int32 at the 2^28 input limit, plus the dB offset and empty-bin
floor. `make test-arm` (run by `test` when qemu-arm, the system arm-none-eabi-gcc with newlib and GCC 10 are there;
`apt-get install qemu-user` as root in WSL) builds `fx_fft.c` with GCC 10 and the firmware flags, links the CLI with
newlib semihosting, runs the same numpy test under `qemu-arm -cpu arm926`, and byte-compares the ARM output with the
C build for 12 sizes and windows. Measured bounds (worst over all signals and windows; limits from the plan: bin error
< -100 dBFS, level error < 0.05 dB):

| N | worst bin error, dBFS | worst level error above -60 dBFS, dB |
|---|---|---|
| 256 | -124 | 0.006 |
| 1024 | -119 | 0.008 |
| 4096 | -113 | 0.008 |
| 16384 | -104.8 | 0.019 |

dBFS here is relative to the bin of a full-scale sine. The level error includes the 0.01 dB output step; the
log2/dB conversion alone is within 1.6e-5 octaves (0.005 dB). Bin-centred full-scale tone: within 0.02 dB of
39.07 dB on all windows; flat-top half a bin off: within 0.05 dB.

### Screens: main menu, USB drive, measurement cards (fw 0.2.51)

The first step of the UX makeover (`docs/plans/ux-makeover.md` section 7 step 1 plus the cards; rules in
`docs/ui-guidelines.md`). Pure parts in `ui_menu.c` (row table, hit areas, mode rule) and `ui_fmt.c` (text widths,
card values, names), host-tested in `host_tests/test_ui.c` against the device's own font tables.

Main menu (Menu button, top left): eight rows since fw 0.2.68 (seven before), always all of them, top to bottom Scope,
Spectrum analyser, Frequency response (0.2.68 as Bode plot; renamed 0.2.69), Saved pictures, Saved waveforms, Settings, Diagnostics, USB drive (were Picture
view, Waveform view, System settings, Diagnostic view, USB connection; Phosphor display left the menu). No marker for
the current mode. Each row is one 52 px hit area from y 46 (row n: 46 + 52 n .. 97 + 52 n; 59 px before 0.2.68), the
35 px button drawn 9 px below its top; the column is 416 px high.
Scope leaves the spectrum view (and brings the phosphor variant back if it was on); Spectrum analyser enters it. In
roll mode Spectrum analyser switches Roll off first (time base 20, the acquisition menu's Roll box does the same) and
the spectrum's warning line says "Roll off for the spectrum" for 5 s; going back to Scope does not switch Roll on again.
Settings opens the System settings fold-out, which lost its X-Y row (Notification confirmation and RTC settings moved
up one row; 354 px, 295 px without a DS3231). Since 0.2.55 the fold-out and the Settings screen below.

USB drive: a header "USB drive", the 1.00o5 picture of the scope and the PC, three lines ("The SD card is now a USB
drive on the PC.", "PC control (MCP) is disconnected until you leave this screen.", "Eject the drive on the PC before
tapping Exit.") and an Exit button (x 610, y 412, 160 x 52), the only control. As before: the SD card is a USB mass
storage drive while the screen shows, Exit with the drive still mounted shows "Not allowed!", Exit otherwise returns
to CDC (PC control) and re-syncs the file lists.

Top bar: no version string any more (Diagnostics keeps the full identity).

Acquisition menu, Display row (0.2.51 only): Normal / Phosphor / X-Y under the acquisition menu until the CTRL page's
Display fold-out existed; 0.2.56 replaced it (below).

Measurement cards (Measures on the right bar; up to twelve per channel): Atlan's 118 x 22 cards in the bottom rows
(CH1 from x 5, CH2 from x 366, three per row, rows upward from y 426), each a grey card with the measurement name in a
chip of the source's colour (full card height, the name plus 3 px each side, rounded only on its outer left corners,
drawn over the card so the join has no notch) and the value 6 px after it, both in the 13 px font. Names as scope
makers spell them: Vmax, Vmin, Vavg, Vrms, Vpp, Vp, Freq, Period, +Width, -Width, +Duty, -Duty (1.00o5: Freg, Cycle,
Time+, Time-, Duty+, Duty-); the Measures menu shows the same names. Values have four significant digits where the
measurement has them and a unit with prefix ("1.000kHz", "500.0us", "-1.190V", "50.00%"); a value with fewer digits of
its own is not padded (a value held as 0.50 V reads "500mV", not "500.0mV"). Freq, Period, the widths and the duties read "no
period" while the record has fewer than three crossings. What the values mean is unchanged (whole-record statistics;
Vrms includes DC). A new unit, and the DEFAULT button, show Freq, Vpp and Vavg on each enabled channel (in the table's
order: Vavg, Vpp, Freq, until 0.2.65 kept the chosen order); 1.00o5 set none. Since 0.2.65 the cards stay in one row
(Measurements v2, below).

### Screens: Diagnostics, calibration wizard, first run (fw 0.2.59)

Step 4 of the UX makeover (`docs/plans/ux-makeover.md` sections 6-6c and 7; rules `docs/ui-guidelines.md`; the review
`docs/plans/diagnostics-review.md`). Pure parts in `ui_diag.c` (each page as boxes, texts and buttons built from the
protocol bodies; host-tested in `host_tests/test_ui_diag.c` with the real 13, 16 and 24 px fonts), drawing and dialogs in
`ui_dlg.c`, the display mode and the actions in `ui_diag_view.c`. The full-screen pages keep the mockups' look: black
ground, outlined boxes and buttons, the Settings screen's black header with a grey rule (0.2.59-0.2.62: a dark blue
band), the state words in colour (green OK, amber Check, red Fault,
grey not available; the word always next to the colour).

**Diagnostics** (Menu > Diagnostics) is a display mode of the main loop, not a modal screen: the main loop keeps polling
the PC protocol (every command works, also the ones that need the main loop) and does the page's pass instead of the
scope's (no acquisition, no scope touch handling, no clock, no class probe). The page is rebuilt from GET_HEALTH, GET_CAL,
GET_LINK_STATS and the card image identity and redrawn once a second ("Updates every second"). The scope screen is kept
in `displaybuffer2`; whatever the PC changes meanwhile (and the MCP chip, the battery icon) is drawn there
(`ui_diag_scope_buffer`, used by `pc_draw_begin`), so Back returns to the current scope screen. Header: "Diagnostics",
the page tabs Status, Calibration, Log, Developer (112 x 44 at x 160, 280, 400, 520) and Back (96 x 44 at x 692, where the
Settings screen has it; x 696 before 0.2.63).

- **Status**: the identity line (fw, git hash, commit time, protocol, USB serial, MMU and caches from the CP15 word), the
  FPGA line (chip, bitstream word and features; amber "FPGA stock bitstream 0x1432: no generator, no filter; gain
  calibration not possible" on the stock bitstream), four tiles with a verdict word and up to five lines (13 px, 15 px
  apart, fw 0.2.100): Firmware (OK
  when the card starts the running image, Check "Running X from RAM. A power cycle starts Y from the card.", no open
  firmware at sector 750, no card), Restarts (fw 0.2.100, maintainer 2026-10-20: crashes only, exception types 1..6; a
  deliberate restart, type 0 "jump to the entry" (a FEL load, Enter FEL, an update, a self-restart), is normal. OK "No
  crashes." "Up 1 h 42 min."; Check, a stored record of a crash before this run: "Crashed once before this run: data
  abort." "If it happens again, save a report." "Up ..."; red Fault, counter 49 > 0 with a real exception in 50:
  "Crashed 2 times this run: data abort at 0x80014a2c." "Details on the Log page."; no LR or exception jargon there,
  the Log page keeps type, LR, PC and fault registers), USB (configured: OK, or Check with bad frames (counters
  12-15) or reconnects (61); Suspended; No PC; then bad frames, suspends, reconnects), Battery ("18 of 20", charging,
  amber "Low: readings and SD writes may fail" below 4 of 20 off the charger, the DS3231 temperature with the module).
  Calibration box: one verdict (Fault: no calibration block; Check: block not bound to the offsets, FPGA changed since
  calibration, gains not measured (stock: not possible), gains carried over, calibrated under 5 minutes after power-on,
  a value over its limit, in two lines: "A value is over its limit (below): move chargers and the USB cable away and
  calibrate again." "If it stays, the input stage is noisy."; OK otherwise), per channel the class gain ratio full /
  gated (limit 2 %) and the worst noise RMS over the ranges, "Noise RMS, inputs open", gated ("limit 1.0 code") and
  full ("limit 3.0 code"), each with OK or Check. The noise rows measure the zero step's open-input condition, so their
  limits are the zero step's pass criterion, one pair of constants (`adc_cal.h` `ACAL_MAX_NOISE16_FULL` / `_GATED`,
  fw 0.2.100; 0.3 / 1.0 code and a "provisional" note before); the provenance sentence ("Calibrated on fw 0.2.59,
  FPGA 0x1535, 12 min after power-on, 1 power-on ago.",
  or why there is none). Buttons: Calibrate (green), Save report to card (the file name, or "Report not saved: no card",
  ... in place of the hint).
- **Calibration**: the block state per channel and the provenance; per channel the table per hardware range (PWM centre
  in use, amber outside 860 +- 100; volts factor in % from 1.00o5's default, amber beyond 3 %; zeros of core A / B gated
  and full), the gains per class and core with the range they were measured on (or "carried over", "not measured"), the
  noise ("Noise RMS, inputs open: gated 0.50, full 1.19 code", amber over a limit, then "Gated limit 1.0 code, full
  limit 3.0 code"), the v4 tracking ("Tracking A-B +0.12 code, slope +0.31 %, 3 s ago", "Full-rate offset +0.70 code, 4 probes,
  12 s ago", grey until they ran), the DC shift values and whether the 1.00o5 core compensations are in use. Buttons:
  Calibrate, DC shift trim (1.00o5's manual trim of the trace scale, `dc_shift_cal`, not saved: while on, CTRL on the
  scope opens the trim pages; was in Settings 0.2.55-0.2.56 and Other settings before; since 0.2.63 the button is green
  while on, grey while off, with "On: CTRL on the scope opens the trim." beside it only while on), Volts calibration (the 1.00o5
  input-divider calibration as a step dialog: both inputs on one DC supply,
  300 mV, 600 mV, 1.5 V, 3 V, 7.5 V, 15 V, retry on "No reading near ...", Save calibration writes sector 708, Cancel puts
  the factors back; the scope's channel, time base and trigger settings are restored either way), Clear calibration (red;
  confirmation "Zeros and gains are cleared; the volts-per-range factors are kept. Readings use defaults until you
  calibrate again. The calibration on the card stops applying at the next power-off.": PWM centres 860, core
  compensations 0, the v3 block unbound in RAM; sector 709 gets the centres at the next power-off, 708 is not written).
- **Log**: the event log decoded in words (the report's decoder, `pc_report_event`), newest first, 17 per page with the
  time since power-on, Older events / Newest events to page through the stored 128; the counters since power-on; Last
  restart (cause, PC, fault status and address, or the record stored by an earlier power-on); SD card (the card image
  against the running one, card size, settings changed or saved); Unit (power-on number, uptime, operating time, how the
  previous power-on ended, settings sector valid at boot). Save report to card.
- **Developer**: "For firmware work. Normal use needs none of this." The Debug overlay row (1.00o5's dev
  mode) only while the flag is set, to switch it off (0.2.63; 0.2.59-0.2.62 had an on / off switch), and Enter FEL now (confirmation
  "The scope restarts into the USB boot loader at once. It waits for a PC to load firmware with sunxi-fel. A power cycle
  boots the card as usual.", green Enter FEL; the ENTER_FEL path, `pc_hal_enter_fel`; the boot-mode byte is not written).

Dialogs own the screen: the screen behind is dimmed to 3/8 and frozen, only their buttons react. Confirmations: a 540 x
320 panel, the consequence in sentences, a large Cancel left (260 x 64), the action right (190 x 44; red only for Clear).

**First run**: at boot, when the calibration is not bound on both channels, a full screen "First start" replaces 1.00o5's
"Load default settings value" screen (gone, with its "Need input calibration procedure !" and "RUN baseline calibration
!"): the identity, three or four checks (FPGA recognised with its features, or the stock exception in amber; settings
loaded or defaults used; calibration missing or not matching the offsets; the volts calibration missing too when sector
708 did not load), what calibrating needs, and Later (left: the scope) or Calibrate now (right: the wizard, then the
volts calibration when it is due). It shows at every boot until the unit is calibrated. A calibrated unit starts with no
screen at all, also after the settings were reset.

Removed with the 1.00o5 diagnostics screen (where its content went): identity line and "fw <version>" (Status identity
line); "fw: PECO" / "fw: Fnirsi" and the AL3-A10 / EF2-L45 labels (Status FPGA line); FPGA version (FPGA line); "Total
samples" (the acquisition menu's memory depth; the stock "Total samples set to 3000" note was a side effect that
`pc_ui_fix_settings` already covers at boot); temperature (Battery tile, module only); ADC comp, DC offset, DC shift,
Offset, Calibration input per range (Calibration page: PWM, volts factor, DC shift, core compensations; dcoffset is the
trace position in the channel menu); "Calibration data: v3 / v1", the per-range zeros, noise, slopes, "now A / B",
gains, "gain at range", the v4 Track and Class lines (Calibration page; the slopes and the correction in use are in the
saved report); DEFAULT (Restore default settings on the Settings screen of step 3); RESTORE (dropped: the card always
holds the last calibration since 0.2.43; "Undo last calibration" is queued); RESET (Clear calibration, which keeps the
volts factors and confirms); EXIT (Back); the hidden boot-mode and option hit areas at x 328-378 (gone).
### Screens: CTRL page, fold-outs, chips, warnings, spectrum cards (fw 0.2.56)

The second step of the UX makeover (`docs/plans/ux-makeover.md` section 7 step 2; rules in `docs/ui-guidelines.md`).
Geometry, hit areas, the value names, the warning texts and the spectrum's card set are pure C in `ui_ctrl.c`
(host-tested in `host_tests/test_ui.c`, also that every text fits its box in the device's fonts); `ctrl_page.c` draws
and runs the touch loops.

Right bar, page one (both modes): CTRL, RUN/STOP, AUTO SET, T CURSOR, V CURSOR, MEASURES, SAVE PIC, SAVE WAVE in
1.00o5's slots. In the spectrum view T CURSOR, V CURSOR and MEASURES are empty (no spectrum markers or spectrum
measurement list yet) and AUTO SET runs the spectrum's Auto (fw 0.2.51). The bar is redrawn whenever the spectrum view
starts or ends.

Right bar, page two = the CTRL page (CTRL highlighted as before), the same order in both modes:

| slot (y) | Scope | Spectrum |
|---|---|---|
| 65 | Display, value Normal / X-Y / Phos / XY Ph: fold-out | Display, value Spec / Wfall / Density: fold-out |
| 125 | Trace style, value Linear / Dots / Peak: fold-out (grey and inert in X-Y since 0.2.63) | Analyse, value the FFT length ("2048 pt"): fold-out |
| 185 | Grid: the whole button toggles (green = grid lines on, grey = off; 0.2.56-0.2.62 a slide switch) | Grid (the plot's dots; the level labels stay) |
| 244..325 | V+ / CH1 / V- stepper | the same |
| 334..415 | V+ / CH2 / V- stepper | the same |
| 425 | 50 % TRIG | empty |

Steppers: 82 px, a 28 px grey V+ bar, a 26 px centre in the channel colour reading CH1 or CH2 (grey while the channel
is off; the value is in the top bar chip), a 28 px grey V- bar. V+ takes y 240..284 and V- 285..329 (CH2: 330..374 and
375..420), so each gets 45 px of touch: the bar, its half of the centre and its half of the gap. They step
`displayvoltperdiv` as 1.00o5's CTRL page did (V+ = the next smaller V/div). Grid is saved (settings word 211) and also
hides the waveform viewer's grid; the record-position slider under the grid always shows.

Fold-outs: a panel left of the bar over the plot's right edge (x up to 727, never over the bar), a grey title, rows of
44 px at a 50 px pitch; the button stays orange while its panel is open. Modal like the top bar's dropdowns (the trace
pauses; a PC request closes it, fw 0.2.78; before, the PC got E_BUSY after 250 ms): a tap outside the panel closes it and does nothing else; a choice row (green
when chosen) closes it; value rows step to the next value and keep it open; a two-way row (Ref level) has - and + boxes
at its ends and each half of the row steps that way. The plot under the panel is put back on close.

| fold-out | rows |
|---|---|
| Display (Scope) | Normal, X-Y (with icons: a clean trace, a Lissajous loop) / a line / Phosphor switch (graded-trace icon; second line the records per second "55 wfm/s", or "Not in roll mode" greyed) and, while it is on, Persistence (0.2 s, 1 s, 5 s, Infinite; a step clears), Palette (Mono, Heat), Clear (clears and closes) |
| Trace style (Scope) | Linear, Dots, Peak (1.00o5's `ACQ_trace`, saved as before; it left the acquisition menu; Pro removed in 0.2.63) |
| Display (Spectrum) | Spectrum, Waterfall, Density (icons: bars, colour stripes, graded dots); saved in word 211; in Waterfall and Density a line and Channel (CH1 / CH2, since 0.2.63) |
| Analyse (Spectrum) | Analyse (CH1, CH2, CH1 + CH2: the shown channel, the dBV / dBA unit and Auto's channel), FFT length, Window, Average / a line / Scale, Ref level (- / +), Reset |

Phosphor is a switch, not a view: it combines with Normal or X-Y (phosphor drawing in X-Y is back after 0.2.51), is
saved (word 210 bit 3), stays on while the spectrum shows (Scope brings the picture back) and pauses in roll mode and
the viewers. Settings sector word 211 (0xC700 marker): bit 0 Grid, bits 1-2 the spectrum's Display variant; the DEFAULT
button sets Normal, Phosphor off, Spectrum, Grid on.

Acquisition menu: acquisition only again (sample rate, time per division, memory, Roll, Average, Filter; 386 px as in
1.00o5). The Trace boxes and the 0.2.51 Display row left it.

Top bar chips: CH1, CH2, ACQ and the trigger are built the same way: a grey box (0x333333) over the whole chip, the
coloured block drawn over its left end with only its outer corners rounded, so the join has no notch (the trigger chip
had its own rounded box). Block colours as before: channel colour (grey when off, black outlined when inverted), ACQ
green (orange in roll mode), trigger green; touched chips keep 1.00o5's white box.

Warnings: one line at the top middle of the plot in orange, consequence and action, in every scope variant (Normal,
X-Y, Phosphor; not in the viewer), the most urgent one only: "No channel enabled: switch on CH1 or CH2", "X-Y needs both
channels: switch on CH1 and CH2", "CHn overrange: increase V/div", "No trigger: check the trigger level" (the latest
record was an auto time-out, while running). The spectrum keeps its own list (below). Readouts such as wfm/s are never
warnings.

Strips gone: the phosphor strip (its fields are in the Display fold-out) and the spectrum strip and header line (the
fields are in the Display and Analyse fold-outs, the readouts are cards). The spectrum's cards sit in one row at y 426
(fw 0.2.71: the Scope's row, the slider row under them; y 446 before), built like the scope's cards (`scope_draw_card`):
per shown channel [Peak] 1.000kHz and [Level] +2.7dBV in the channel colour, then [RBW] 146.5Hz and, in the waterfall,
[Time] 12.30s (the time its rows span) with a light grey chip; at most six. The decimation filter tag is no longer shown
(the acquisition menu has it). The waterfall's and density's colour legend stays above the plot's top right. Since 0.2.65
the spectrum's cards are chosen through MEASURES (below).

### Screens: Measurements v2 (fw 0.2.65)

Plan `docs/plans/ux-makeover.md` section 3a, the review's measurement part (`docs/plans/ux-review.md` section 6 part B).
The model is pure C in `ui_meas.c` (host-tested in `host_tests/test_ui.c`); `scope_functions.c` draws the Scope cards,
`spectrum.c` the spectrum's, `menu.c` / `statemachine.c` the MEASURES menu.

- **One list per mode.** MEASURES opens the list of the mode on screen: in the Scope (and the waveform viewer) the twelve
  measurements per channel, in the Spectrum analyser (where the slot is no longer empty) Peak, Level, THD and Noise per
  channel and RBW, Span, Center, Time (the waterfall's history span) and Capture (fw 0.2.84: the time one record takes,
  memory depth / fs) for the view. The Scope's choice is kept where 1.00o5 kept it (the `measuresstate` bytes of the
  settings sector); the spectrum's in settings words 221..223, Capture's rank in word 217 (a sector without it puts
  Capture last).
- **Order.** The cards show in the order they were chosen: a measurement turned on goes last in its row, one turned off
  closes its gap. Each chosen item shows its place at the top left of its cell in the menu. Select All keeps the chosen
  ones first and adds the rest in table order. The defaults (a new unit, DEFAULT, Restore default settings) are Freq,
  Vpp, Vavg per channel in that order; in the spectrum CH1 Peak, Level, THD, CH2 Peak, Level, then RBW and (waterfall
  only) Time. A settings sector written by an older image shows its cards in table order (1.00o5 stored 1 for every
  chosen card); a 1.00o5 image reading ours sees the same choice.
- **Fit.** The cards keep to one row and no longer stack upward over the trace: the Scope's at y 426, CH1 three from x 5
  (six, the whole row, while the CH2 block is not shown: CH2 off with no reference trace, or Hide), CH2 three from x 366;
  the spectrum's six from x 5 at y 426 (446 before fw 0.2.71). With more chosen than fit, the last place reads "+N more" (N the cards not shown)
  in the block's colour, so nothing is dropped silently.
- **Spectrum values.** Peak, Level and RBW as before. THD: the power in the three bins around each harmonic 2..10 (each
  searched within n k +- (n / 2 + 1) bins of n times the peak bin, narrowed so neighbours never share a bin) against the
  three bins around the peak, over the whole axis (harmonics may lie outside a zoomed span), from the shown (averaged)
  levels; "f0 too low" when the peak is below bin 5, "f0 too high" when the second harmonic is past the axis. The 8-bit
  ADC puts its floor near 0.3 % (about -50 dBc). Noise: the median bin level of the latest record (not averaged) over
  the shown span, +1.59 dB (the median of a noise bin's power lies 10 log10(ln 2) below its mean), in dBV (dBA) at the
  RBW shown on the RBW card; strong tones do not move a median. Span and Center are the shown span and its middle.
- **MEASURES menu.** 1.00o5's box (x 232..730, dark grey, white edge, black separators, chosen names white and the others
  grey) with a title band ("Scope measurements" / "Spectrum measurements", 16 px) and 44 px rows, ending at y 477.
  Scope: the source row (CH1 or Ref 1-4, CH2 or Ref 5-8; the chosen source filled in its colour), Select All / Clear /
  Hide (in the source colour; Hide in the active orange while the block is hidden), three rows of four measurements.
  Spectrum: the channel label with Select All / Clear, the channel's four items, and the view's four items across the
  whole width. A cell acts on touch and the menu stays open; a tap outside closes it (and does nothing else). Every tap
  below the title band lands on a cell (the separators belong to the cells beside them).
- **Card sources (hardware counter hook).** A Scope card of a live channel takes its value from a source registered for
  its measurement (`ui_ms_set_source(UI_MEAS_FREQ, fn)`, `ui_meas.h`) instead of the record statistics; reference traces
  always use their record. A source writes the card's text for CH1 or CH2 with the card formatters (`ui_fmt_sig4`,
  "no period") and must only read latched values (it runs while the screen is drawn). An FPGA frequency / period
  counter (`docs/plans/roadmap.md` section 8 item 6) registers itself at start-up when the bitstream reports it; the
  cards, the menu and the settings need no change. Nothing registers one yet.

### Settings: fold-out, Settings screen, confirmations (fw 0.2.55)

Step 3 of the UX makeover (`docs/plans/ux-makeover.md` section 5, dialogs 6b). Tables, values, hit areas, the start-up
choice and the dialog layout are pure C in `ui_settings.c` (host-tested in `host_tests/test_ui.c`: every pixel of every
hit area against the drawn rectangle, every value text against its row in the device's fonts); the screen and the
dialog are drawn and run in `ui_settings_ui.c`; the fold-out is drawn in `menu.c` and hit-tested in `statemachine.c`
from the same table.

Settings fold-out (Menu > Settings), 59 px rows from y 46, contiguous hit areas: Screen brightness and Grid
brightness (sliders beside the fold-out, as before; since 0.2.63 a drag that starts on a slider stays with it until the
finger lifts), Time and date (only with a DS3231; opens 1.00o5's editor beside the fold-out with hours .. year and RTC
On/Off) and Other settings (opens the Settings screen). 236 px high, 177 px without a DS3231. (0.2.55-0.2.62 also had
File message timeout here; since 0.2.63 it is in Settings > Controls only.) (0.2.55-0.2.56 had an interim Baseline calibration row here: fw 0.2.59 moved it to Diagnostics >
Calibrate, together with the 1.00o5 volts-calibration boxes that opened beside the fold-out after the wizard.)

Settings screen (fold-out > Other settings; a header with the title and Back only, since 0.2.63): a modal full-screen page like the menus (a PC request leaves it as Back
does, fw 0.2.78; before, the PC protocol answered E_BUSY while it was open), black ground, outlined 48 px rows in two columns, the value of each row on its right in the state
colour (green in use, grey off or not available, amber needs attention). Back (top right) returns to the scope as it
was. Every row is an existing setting, saved at power-off as before unless noted:

| group | row | values | setting |
|---|---|---|---|
| Display | Screen brightness | 0..100 %, - / + by 5 | `screenbrightness`, the same value as the fold-out slider and the top bar sun (fw 0.2.68) |
| Display | Grid brightness | 0..100 %, - / + by 5 | `gridbrightness` |
| Controls | File message timeout | wait for a tap / 0.7 s | `confirmationmode` |
| Controls | Cursors | move together / move freely | `lockcursors` |
| Controls | Auto setup | improved / stock | `new_autosetup` |
| Files | Stamp screenshots with | time / file name / nothing | `tag_in_BMP`; time with the clock on, else the file name |
| Files | Reference traces at power-on | loaded / not loaded | `ref_on_startup` |
| Clock | Clock | shown / not used / no clock module | `onoffRTC` (top-bar clock, file stamps, FAT times) |
| Clock | Time and date | 2026-10-19 11:30 / needs the clock / no clock module | opens a page with Hour, Minute, Day, Month, Year and - / + (96 x 44 px); each tap is written to the DS3231 at once |
| Start-up | At power-on | Open firmware / Boot menu, then open firmware / Boot menu, then stock firmware | boot-mode byte 0 / 4 / 5 (section 3); written to sector 710 on Back, only when changed |

The start-up row never writes FEL (2) or the updater (3); a byte that holds one of them (or 1, stock without the menu,
which only the loader tool writes) is shown in amber ("FEL, no boot menu", "Boot menu, then FEL", "Updater", "Stock
firmware, no boot menu") and the next tap sets Open firmware. The stock image is offered only with the boot menu.

Restore default settings (bottom left, no note beside it since 0.2.63): asks first ("Restore default settings?" with the consequence on two full lines: channels,
trigger, time base, display and these settings go back to their defaults, the calibration and the start-up choice are
kept). Restore runs `scope_reset_config_data` (sector 709's settings to defaults, the calibration reloaded from sector
708), sets the hardware up from them as at power-on and rebuilds the scope screen. Since 0.2.55
`scope_reset_config_data` no longer writes the boot-mode byte (1.00o5 set it to 4 on every reset, also when the
settings sector failed to load), and in the CDC build the reset keeps USB in CDC mode.

Confirmation dialog (`ui_confirm`): a 520 px box in the middle of the screen, a 24 px title, up to three 16 px lines,
two 160 x 44 px buttons 16 px apart, the dismissing one left and the action right, red only for a destructive action.
Only the two buttons react. Used by Restore default settings and by every delete in the file browsers and the
waveform viewer ("Delete from the SD card?", "This cannot be undone.", Cancel / Delete; was "Confirm to delete?" with
74 x 50 px NO / YES 11 px apart).

Where the controls of 1.00o5's Other settings menu went (the menu is gone):

| control | now |
|---|---|
| Boot menu (on / off) and Default start (PECO / FNIRSI / FEL) | Settings > Start-up > At power-on (three values; FEL is no longer offered: ENTER_FEL from the PC, the boot menu's FEL button, and the step-4 Developer page's "Enter FEL now") |
| New autosetup | Settings > Controls > Auto setup |
| Lock move cursors | Settings > Controls > Cursors |
| DC shift calibration | Diagnostics > Calibration > DC shift trim (fw 0.2.59; Settings > Calibration in 0.2.55-0.2.56) |
| USB-Serial mode | removed: the build forces CDC at boot (`PC_FORCE_CDC`), the flag did nothing visible |
| Dev mode | a developer flag only (fw 0.2.63; Diagnostics > Developer > Debug overlay 0.2.59-0.2.62): stays in the settings sector with its effects (trigger address overlay, touch coordinates); the Developer page offers to switch it off while it is set. Its exception to the boot-menu rule is gone: At power-on writes exactly the chosen value |
| Add a tag to BMP | Settings > Files > Stamp screenshots with |
| Load Ref on start | Settings > Files > Reference traces at power-on |
| RTC On/Off (RTC editor) | still in the editor; also Settings > Clock > Clock |

### Screens: UI polish batch 2 (fw 0.2.68)

The maintainer's walk-through of 0.2.65 (2026-10-19 18:00). What changed:

- RUN/STOP: one run state (`ui_run_view`, ui_ctrl.h) for the top bar chip and the right bar button. It is the UI's
  run state, its single-shot wait, and a host acquisition in progress: a host ARM armed or triggered, or one that
  finished less than 1 s ago, so a burst of host captures (averaging, a Bode sweep) reads RUN instead of flickering.
  The chip shows the state (green RUN, orange WAIT for an armed single, red STOP). The button shows the action a tap
  does (maintainer, reverses 0.2.63): green "RUN" while stopped, red "STOP" with white text while running or waiting (an maintainer
  exception to red-is-destructive, `docs/ui-guidelines.md` section 3). Every main-loop pass redraws whichever of the two
  shows a stale state (`pc_run_follow`), whoever changed it: the screen, a finished single shot, the host's RUN, STOP or
  ARM. STOP during a host acquisition aborts the host single (its WAIT answers E_ABORTED) and the UI stays stopped
  (before, a tap there started the UI's own RUN).
- Main menu: Settings shows the spanner and screwdriver of the Other settings row (the drawn cogwheel read as a star).
  The Scope sine is 2 px shorter (y 9..28 of the row's button; it reached the top before) and drawn over x / y axes
  through its centre in a darker grey (0x707070).
- Time/div snap after a sample rate, memory or Filter change: the snap now takes the same path as choosing the time/div
  by hand in the ACQ menu (`acq_view_set_tb` and `acq_view_fit` share `acq_view_set`: the index, the trigger point kept
  inside the record, then a fresh UI conversion). test_tb_view checks that the state after a snap equals the state
  after a manual set of the same index, for every rate, depth, trigger sample, start time/div and trigger point.
- Screen brightness is one setting: the top bar's sun switches to 100 % and back to the value it came from; the
  fold-out's slider and the Settings screen's - / + show and set the same value. The sun is yellow at 100 % and white
  below, and follows the slider. 1.00o5 kept a separate max-light flag: the sun switched the backlight to maximum while
  the slider kept showing (and, when opened, restored) the stored value. The settings sector keeps the value the sun
  returns to in 1.00o5's max-light byte as 0x80 | value; an older sector with the flag on loads as 100 %.
- Calibration wizard: "Step M of N" at the right end of the title line (four screens, three on a bitstream without the
  generator).
- Diagnostics > Calibration: Calibrate, DC shift trim, Volts calibration and Clear calibration are one row at the
  bottom (y 420, as on the other tabs), four 185 x 44 px buttons 12 px apart across the page, Clear calibration last.
  The trim's "On: CTRL on the scope opens the trim." and the page's note sit above the row.

### Screens: UI polish batch 1 (fw 0.2.63)

The maintainer's walk-through of 0.2.59 (`docs/plans/task-queue.md` "UI polish batch 1", items a-q). The sections above say
what each screen does; this is what changed:

- Main menu icons: Settings was a drawn cogwheel (replaced by the Other settings spanner in 0.2.68), Scope one period of
  a sine wave (25 px, 1 px stroke; axes behind it since 0.2.68). The 1.00o5 bitmaps `system_settings_icon` and
  `confirmation_icon` are gone.
- CTRL page: Grid is a whole-button toggle (green on, grey off, the word "Grid"). Trace style is grey and does nothing in
  X-Y (the Lissajous has its own line). The Display fold-out's X-Y icon is a figure 8 leaning right. Trace style offers
  Linear, Dots, Peak: 1.00o5's Pro drew the per-column minimum and maximum as two outlines without the fill between
  them; on a normal signal both lie within a pixel and it looked like Linear, on a busy one it showed Peak's band
  without the fill. A saved 3 loads as Linear.
- RUN/STOP (right bar) showed the state: green "RUN" while running, amber "Stopped" while stopped. Replaced in 0.2.68
  (the button shows the action, the chip the state; see "UI polish batch 2").
- Spectrum Display fold-out, Waterfall and Density: a Channel row (CH1 / CH2) names the channel the history shows; a tap
  takes the other one (with Analyse CH1 + CH2 both spectra stay computed and the history follows the choice; with a
  single-channel Analyse, Analyse follows). Grey when the other channel is off. Not saved, like Analyse. One history at
  a time; both channels' histories at once is a later step.
- Settings fold-out without File message timeout; the brightness sliders keep a drag until release (a finger drifting
  off the 58 px slider band closed the fold-out or hit a main-menu row). Settings screen without the header hedge and
  the note beside Restore; the Restore dialog's two lines fill the dialog's width (test_ui checks no line could take
  the next word).
- Diagnostics: the Settings screen's header (black, grey rule, Back at x 692), also on the first-run screen. The
  battery icon is drawn into the kept scope screen while Diagnostics shows (it was drawn over the page's header).
  Calibration page: the DC shift trim button is green while on, grey while off; "Needs a DC supply up to 15 V." is gone.
  Developer page: the Debug overlay switch is gone (the row shows only while the flag is set, to switch it off).
- Volts calibration step dialog: "Step N of 6" at the right end of the title line; the second line reads "Tap Calibrate.".
- ACQ menu: the pass time sits in the Memory column under its rule, "Pass time" and "NN ms" on two lines.

### Touch on the scope plot (fw 0.2.54, batch 2 in 0.2.64)

`scan_for_touch` (statemachine.c) picks once, at the touch-down point (`scan_plot_pick`); geometry in `touch_pick.c`
(pure, host-tested in `host_tests/test_touch_pick.c`). Normal display, in this order:

1. **Left handle column** (x 0..43): CH1, CH2 and the eight reference pointers each own the 44 rows around their tip
   (`scope_pointer_top(position) + 6`, the clamped position the pointer is drawn at); the nearest switched-on pointer
   wins, a tie to the one drawn on top (draw order CH1, CH2, ref1..ref8). A drag moves it vertically.
2. **Record-position slider** (fw 0.2.73: a line at y 452, the track x 2..727 / y 453..471; hit area from y 450, and up
   to y 436 when nothing else is near: 44 px): the window follows the finger, dragging right shows later samples (the
   trace moves left), limited to the record (`acq_view_slider_pos`). A touch-down on the row (y 450 down) outside the
   window first centres the window there (tap to jump, `acq_view_slider_jump`, called in `scan_for_touch` at the
   touch-down since fw 0.2.76, so a still tap jumps too); the extended area above the row never jumps. Short mode only.
3. **Trigger level pointer** (x > 660, 30 px around its line), only while it is drawn.
4. The nearest of: a **channel** (CH1 / CH2 switched on, x 44..685), measured to the nearer of its drawn trace
   (`trace_distance` over the polyline as drawn, the Peak / Pro min-max bands included; in the phosphor view only the
   zero line) and its zero line, within 40 px, a tie to CH2; the **time cursors** (30 px in x) and **volt cursors**
   (30 px in y, x > 60), a tie to the cursor.
5. Nothing: a horizontal drag pans the trigger point (limited to the record in short mode).

A touch becomes a drag after 6 px (`TOUCH_DEAD_BAND`); a lift inside that within 250 ms is a tap. Handles (pointers,
slider, trigger level) never tap; on a trace or cursor a tap counts only when the finger moved 2 px or less. Taps on
the empty plot keep 1.00o5's bands (maintainer), contiguous now (`touch_tap_zone`): left / right half (x < 358 / > 362),
y 50..99 V/div one step more sensitive, y 100 up to the lower band time/div slower / faster, the lower band (80 rows)
V/div less sensitive. The lower band ends at the grid's bottom (449) or at the top of that half's measurement cards
(rows of three from y 426, 24 px pitch); on the slider row only the jump above. A channel drag moves the trigger level on that
channel (CH1 for source 0, CH2 for 1, never for EXT) by the same delta, limited so both stay in 6..396. In X-Y every
drag moves CH1 with x and CH2 with y. The touch loop still blocks until the finger moves or lifts (touch-review batch 3).

Spectrum view: the pan divides by 5 when Move is slow and uses the same dead band and tap time. CTRL fold-out rows act
on release, only when the finger is still on the row; a tap outside closes the fold-out and does nothing else.

### Time/div, sample rate and the record (fw 0.2.64)

The trace is a window onto the record: 50 px per division over columns 3..726, the trigger point at column
`triggerhorizontalposition`, first sample `trig - 1 - ceil(pos / xpps)` (1.00o5's formula). `tb_view.c` (pure,
`host_tests/test_tb_view.c`) derives from the real record length (`fpgasettings.totalsamples` x sample period):

- a time/div is **viable** at a rate when the record spans the 724 px trace area and a division holds at least ten
  samples (fw 0.2.69, maintainer: one sample per division drew a wobbling interpolated edge; 200 MSa/s now reaches 50 ns/div,
  and 20, 10 and 5 ns/div snap to 50 ns/div; at 3000 samples: up to 207 samples/div, so 200 MSa/s reaches 1 us/div;
  15000 samples reach 5 us/div);
  1.00o5's hand-written `viable_time_per_div` table is gone (it offered 250 samples/div, a record shorter than the
  screen: zeros past its end; the test lists the 24 cells that differ);
- the **trigger point limits** that keep the window inside the record, the **drawn window** (the first sample clamped
  into the record; a record shorter than the screen is drawn from the left edge and stops at its end), and the
  **slider** to scale (window width = the part on screen).

`acq_view.c` applies one rule for every writer: the ACQ menu (a rate change snaps the time/div to the nearest viable
one and shows "Time/div set to X for this rate" in white at the top middle for 4 s after it is first drawn; grey
time/div boxes cannot be chosen; memory depth and Filter changes refit), the plot's tap zoom while stopped (it stops
at the record's limits), the host's SET_TIMEBASE mode 0, SET_TRIGGER pretrigger / total and SET_DECIM, and the
settings load. Long (roll) mode is left as it was.

**Time/div drives the sample rate (fw 0.2.76**, docs/ui-guidelines.md section 2). While running or in Single, a tap
zoom step and an ACQ menu time/div box set the time/div together with its ladder rate (`tbv_ladder_rate`: the fastest
short-mode rate at which the time/div is viable for the current record length), so the rate falls when zooming out
past the record and rises when zooming in past ten samples per division; a time/div box is grey only when no rate can
show it (20, 10, 5 ns/div). With 3000 samples the ladder runs from 200 ms/div at 1 kSa/s to 50 ns/div at 200 MSa/s
(1 ms/div at 200 kSa/s, 200 samples a division); deeper memory moves every step to a faster rate. A rate box still
sets the rate by hand (the time/div snaps with the notice when needed) until the next zoom step. The ends are plain
stops and the tap never crosses into or out of roll. Stopped or in the viewer, the rate is the record's: the zoom
steps through what the record can show and says "Stopped: press RUN to zoom further" where only a new record could
go on. `acq_view_zoom`, host test `host_tests/test_acq_view.c` (acq_view.c built on the host with stub globals).

### Spectrum view (`spectrum.c`, fw 0.2.28)

A display mode of the main loop, not a modal menu, so the MCP's polling and the shared screen keep working (plan:
`docs/plans/dpos350p-features.md` 2b). Main menu, row "Spectrum analyser"; the row "Scope" returns (fw 0.2.51; in roll
mode the row switches Roll off first, see "Screens" above). While on, `scope_display_trace_data` (hook `PC_SPECTRUM_DISPLAY`) draws the trace area itself and touches
on the plot go to its gestures (`PC_SPECTRUM_TOUCH`); the top bar and the MCP chip are 1.00o5's, the right bar shows the
spectrum's slots and CTRL page (fw 0.2.56, "Screens" above). Each new capture (UI or host) is transformed once; settings changes recompute from the held buffer,
so a stopped capture can be re-examined. Roll mode or a viewer ends the view.

Screen (fw 0.2.56; fw 0.2.71 per ux-makeover.md "Spectrum slider"): plot y 50..402, 8 divisions of 44 px with dBV labels
(dBA in A mode); vertical grid lines at the 1-2-5 frequency steps of the span with their labels under them at y 406
("2.5kHz"; every other one labelled when more than six; the grid rule, docs/ui-guidelines.md section 3; start / centre /
stop labels and ten fixed divisions before); the cards at y 426 (peak frequency and level per channel, RBW; "Screens"
above); the slider row at y 452..471 in the Scope's construction: the shown span as a window over the axis 0 ..
min(fs/2, 25 MHz) (the window fills the track at the full span), a drag pans the centre with the span kept (the window
follows the finger, as the Scope's record slider; since fw 0.2.77 a pan step can change the rate and so the track's
scale: the finger's travel keeps the scale the track had at touch-down). The waterfall's and density's colour legend sits inside the plot's
top right (y 72). The info line and the strip went in 0.2.56. Since 0.2.73 the track shows the whole axis's spectrum
(below, "Slider track overview") and a tap outside the window centres it there.
Each plot column shows the highest bin it covers, so narrow lines survive at 720 columns; zoomed in past one bin per
column it draws straight lines between bins. A white triangle marks the peak (DC bins skipped). Below 200 MS/s an orange line says
"no anti-alias filter": the FPGA decimates by skipping samples (until a filtering bitstream lands).

Parameters, in the CTRL page's Analyse fold-out since fw 0.2.56 (a strip under the plot before; View is the
Display fold-out):

| row | values | tap |
|---|---|---|
| Analyse | CH1, CH2, CH1 + CH2 (only enabled channels) | next |
| FFT length | 1024 .. the largest power of two the record holds, at most 4096 (2048 at depth 3000); "2048 (record)" when the record holds less than the length chosen (fw 0.2.77) | next, wraps |
| Window | Hann, flat top, rect | next |
| Scale | 5, 10, 20 dB/div | next |
| Ref level | -100 .. +60 dBV at the top line, one division per step | left half (-) down, right half (+) up |
| Reset | back to the defaults: 2048 points, Hann, 10 dB/div, ref 0 dBV, the current rate's whole axis (fw 0.2.77), Spectrum | once |
| Display (its own fold-out) | Spectrum, Waterfall, Density (fw 0.2.32) | choose |

Auto (sets FFT length, window, scale, ref level and span from the signal, rules below) is the right bar's AUTO SET
while the view shows (fw 0.2.51; a strip field before): the button reads "Auto..." while it runs, and a second tap, or
any touch on the plot, cancels it. Leaving the view ends it.

With the 0x1534 decimation filter on (fw 0.2.34) the view transforms the filter's 16-bit record (`pc_hal_trace16`)
instead of the 8-bit trace, with the filter's rate 200 MHz / N (N = 2 (floor(D / 2) + 1), so even dividers differ from
the time base's D + 1), one 16-bit unit = `scale_q24` / 2^24 codes for the dBV term, and the zero code moved into the
16-bit domain; the orange aliasing warning is not shown (the info line's filter tag went with the line in 0.2.56;
READ_SPECTRUM reports the filter). Not benched (the bitstream is not flashed yet).

Waterfall and density (fw 0.2.32) show one channel (CH1 unless only CH2 is selected or on). Waterfall: each new
capture adds one row at the top (the plot holds 336), coloured over the plot's 8 divisions from the bottom line
(black) to the ref level (white), the colour legend above the plot's top right, the time the rows span on a card.
Density (fw 0.2.35): each capture's spectrum line adds hits to a 720 x 336 grid. A cell keeps an 8-bit count and the
16-bit index of the capture of its last hit; a hit decays the count to now (exp(-age / 16 captures), Q8 table) and
adds 32 (saturating), drawing decays it the same way, so the persistence is uniform in time and position with no
fading pass (0.2.33's one-stripe-per-capture fade showed as a band sweeping down the screen). A cell hit in every
capture saturates white; one hit in 10 % of captures sits around dark red; a cell 256 captures old is cleared when
drawn. Colour ramp (legend "rare or old" .. "frequent" above the plot's top right): black, dark blue, red, yellow,
white. The level grid and labels stay.
Both histories clear when anything that changes what a column or level means changes (FFT length, window, span,
start, ref, scale, sample rate, channel), so a view never mixes axes; in STOP they wait for RUN and say so. The
rows are stored as RGB565 (drawn with memcpy bursts) and the counts as words with zero words skipped, since before the
MMU (0.2.48) every uncached pixel access cost about 90 cycles. Memory: 484 KB waterfall + 726 KB density (.bss).

Axis cap (fw 0.2.36, maintainer): the view shows at most `PC_SPECTRUM_FMAX_HZ` = 25 MHz (spectrum.h), or fs/2 when that
is lower; the span ladder's whole axis is 0-25 MHz (at 50 MSa/s since fw 0.2.77) and zoom, pan and Auto stay inside it. The front end rolls off near
30 MHz, so above 25 MHz there would only be artefacts. The info line still states the sample rate, and READ_SPECTRUM
still returns every bin up to fs/2.

Gestures on the plot (fw 0.2.35, the scope view's rules from `scan_for_touch` in statemachine.c): a touch that stays
within 2 px and ends within 200 ms is a tap; left of the plot centre widens the span one ladder step, right of it
narrows it, keeping the centre, like the time/div taps in the scope view. A touch that moves more than 2 px is a drag:
the spectrum follows the finger horizontally (the centre moves, in mHz) and is redrawn while dragging; at the whole
axis there is nothing to pan. Each tap and each drag move is a step of the span ladder below.

Header and averaging (fw 0.2.40-0.2.42, maintainer; since 0.2.56 the header's readouts are cards, see "Screens"): per
shown channel its peak frequency and level, the RBW and, in the waterfall, the time its rows span. No fs or N (they are
the time base and the FFT length). Warnings appear centred over the top of
the plot in orange only while they apply: "CHn overrange", "no anti-alias filter below 200 MS/s" (decimated without
the 0x1534 filter), "Stopped: RUN fills the view" (history views, or no record). The plot is 8 divisions of 43 px.
Average field: Off, 4, 16, 64 (exponential average of the dB levels over a growing count up to that many captures,
x16 fixed point, so the first captures converge at once) or Max hold (the highest level per bin). Default 4. It is
applied before drawing and before READ_SPECTRUM (header byte 3 bits 4-6: 0 off, 1-3 = 4 / 16 / 64, 4 max-hold) and
restarts whenever a bin's meaning changes (FFT length, window, sample rate, filter, range or probe, V/A, the mode
itself); display-only changes (ref, scale, span, pan) keep it, as on a bench analyser. Averaging dB is not averaging
power: on noise it reads about 2.5 dB low (log of the mean vs mean of the log), which is fine for steadying a
display; use the host for calibrated noise power. The waterfall and density views take the unaveraged records. The
averaging mode persists with the other settings (word 209 bits 12-14; ref in bits 0-11).

Span and sample rate, the zoom ladder (fw 0.2.77, maintainer 2026-10-19 23:09; the rule is docs/ui-guidelines.md section 2
"Span drives the sample rate"; `spec_ladder.c`, host-tested by `host_tests/test_spec_zoom.c`). The view is a cell: a
span from the ladder 1, 2, 5 Hz .. 10, 20 MHz, then the whole axis 0 .. 25 MHz, about a centre kept inside 0 .. 25 MHz.
A tap steps the span one cell about the centre, a drag (plot or slider) or a tap in the slider's track moves the centre.
After every zoom step, while a new record can be taken (running, or Single), and at the end of AUTO SET, the rate is set
from the stop frequency (centre + span / 2): the slowest UI rate (200 MSa/s .. 500 Sa/s) whose fs / 2 covers it and
whose capture of the record (memory depth samples; fw 0.2.84, N samples before) takes at most 2.048 s (`spl_rate`). The rate is the scope's own
setting, written with the time/div the Scope's ladder pairs with it (`acq_view_tb_for_rate`), so going back to the Scope
shows a ladder pair. After a zoom step the pair is a function of the cell, whatever led there; a cell from 0 Hz holds
between a quarter and a half of the N bins. A pan never changes the rate (fw 0.2.81, maintainer 2026-10-20): it is clamped to
the current axis 0 .. fs / 2 (the slider track), so to look above it zoom out (the rate rises) and in again; a pan to a
low centre at a wide span keeps the coarse resolution until the next zoom step. Ends, plain stops (a tap there changes
nothing, the other direction works at once): zooming out ends at the whole axis at 50 MSa/s; zooming in ends where the
next span would need a rate whose capture exceeds 2.048 s or would hold fewer than 16 bins. In numbers: from the whole
axis (centre 12.5 MHz, 50 MSa/s) the narrowest span is 1 MHz at N 1024, 500 kHz at 2048, 200 kHz at 4096; at 0 Hz the
memory depth ends it (fw 0.2.84): 1 kHz at 2 kSa/s with 3000 samples (1.5 s), 2 kHz at 5 kSa/s with 6000 or 9000, 5
kHz at 10 kSa/s with 12000 .. 16382 (16k at 2 kSa/s took 8 s per record before). A pan stops at the axis end (1 kHz span at 1.5 .. 2.5 kHz runs at 5 kSa/s, whose axis ends at 2.5 kHz: a
pan up changes nothing; panned to 0 .. 1 kHz it stays at 5 kSa/s, and a tap right there gives 500 Hz at 2 kSa/s). After
a rate change the view waits for the first capture at the new rate ("Waiting for a capture at the new rate") and the
histories clear (as they do whenever the drawn window moves, a pan included: their rows are bins of that window). A rate
picked in the ACQ menu (or by the host) holds until the next zoom step; the view then shows the cell on that rate's axis
as far as it reaches (the Span and Center cards show what is drawn), a pan clamps the cell into that axis, and the next
zoom step takes the ladder's rate. Changing the memory depth (ACQ menu depth or Filter, the host) sets the ladder's rate
for the cell at the new depth at once while the rate is the one the ladder set (fw 0.2.84); a hand-picked rate holds.
Changing the FFT length does not change the rate (N is not in it); it moves the zoom-in end.
Stopped (the record on screen is fixed), zoom and pan stay on the record's rate and axis, and where the ladder would go
on the warning line says "Stopped: press RUN to zoom further" for 4 s. Entering the view keeps the last cell if the rate
is still the ladder's for it or the one the cell was last set at (after a pan), else shows the current rate's whole axis
without changing the rate (at 100 and 200 MSa/s the whole axis's rate is 50 MSa/s, taken at the first step). Before
0.2.77 (fw 0.2.39-0.2.76) zoom in halved the span on the same rate and changed the rate only below N/16 bins, and zoom
out changed it only past the whole axis; in 0.2.77-0.2.80 a pan also re-derived the rate (janky mid-scroll on the
bench).

Persistence (fw 0.2.39): FFT length, window, scale and ref level are saved in settings-sector words 208 (0x5E00
marker, window, scale, length) and 209 (ref, int16), with everything else, by 1.00o5's power-off interrupt (the only
place 1.00o5 writes the settings sector, apart from a failed load; not in the picture / waveform viewers, not on
ENTER_FEL or a battery that dies). Not restored: the display mode (the scope boots into the scope view), channel,
span and centre; the variant (Spectrum / Waterfall / Density) is restored since fw 0.2.56 (word 211). The Filter choice (fw-decim, word 207) is saved the same way.

Auto (fw 0.2.42, maintainer: deterministic, identical presses give identical settings): a fixed probe procedure that
does not depend on the state it starts from, about 1-2 s, "Auto..." on AUTO SET and the plot meanwhile, any touch
cancels, gives up after 4 s with what it has. It needs RUN and trigger mode Auto or Normal (otherwise a warning says
so). Steps:
1. Probe: 50 MSa/s (2 us/div), the longest FFT the record holds up to 4096, Hann, averaging 4, trigger source = the
   analysed channel (CH1 unless only CH2 is shown; an EXT source stays), level set to 50 % on the first record
   (1.00o5's `scope_do_50_percent_trigger_setup`); then 3 records averaged. Untriggered records are skipped while
   triggered ones come.
2. Measure f0 = the fundamental from the time-domain period (rising crossings with the readout's hysteresis levels;
   0 with fewer than 3 periods in the record) and f_hi = the highest bin above both peak - 40 dB and median + 10 dB.
3. Period search: while f0 = 0, step 10x slower (three time base steps) per record until 3 periods fit or the
   slowest short time base.
4. Derive from (f0, f_hi) only, on the span ladder (fw 0.2.77, `spl_auto`): the narrowest ladder span from 0 Hz at
   least max(1.25 f_hi, 20 f0) (the whole axis above 20 MHz); with f0 > 0 one span narrower at a time while RBW =
   1.5 fs / N > f0 / 4 at the ladder's rate for it, never past the zoom-in end; the rate is the ladder's for the cell.
   (Before 0.2.77: rate = slowest with fs/2 >= 1.25 span, slower for RBW, span trimmed to 0.8 fs/2.)
5. Confirm with 2 records at that rate; if the band seen there is less than half the probe's (the probe sees edges
   and noise far up) and that asks for another rate, one refinement with 2 more records.
6. Final: ref = the first multiple of the scale at least 3 dB above the peak, scale 5 / 10 / 20 dB/div by whether
   peak - floor + 10 dB fits 8 divisions, the cell from step 4 and its rate, averaging 4. The result is a cell of
   the ladder, so taps from it go on along the ladder.
Expected for the 1 kHz square at depth 3000 (N = 2048), derived from the rules (not benched on 0.2.77): probe f0 = 0,
the search finds 1 kHz at 500 kSa/s, the RBW rule (<= 250 Hz) narrows to 100 kHz from 0 at 200 kSa/s (half the axis
of bins): the comb fills the plot. At depth 6000 (N = 4096) 200 kHz at 500 kSa/s.

Warnings (fw 0.2.42, maintainer: consequence and action, one short line each, centred over the top of the plot in orange,
only while they apply; "MSa/s" everywhere as in the top bar):

| warning | when |
|---|---|
| CHn overrange: increase V/div | the shown channel's last readout clipped |
| Aliasing possible: set Acquisition > Filter | below 200 MSa/s with the Filter off (0x1534+); "use a faster time base" without the filter |
| Stopped: press RUN to fill the view | stopped in a history view, or no record yet |
| Auto needs RUN: press RUN / Auto needs trigger mode Auto or Normal | Auto pressed while stopped / in single mode |
| Stopped: press RUN to zoom further | 4 s after a zoom tap while stopped where the record's rate ends the zoom but the ladder would go on (fw 0.2.77) |
| Waiting for a capture at the new rate | after a span-driven rate change, until the first record at the new rate |
| No trigger: check the trigger level | the latest record was an auto time-out (fw 0.2.41) |
| No channel enabled: switch on CH1 or CH2 | centre of the plot, both channels off |

Levels are dBV of a sine's RMS (a tone centred on a bin reads its RMS level on every window; noise reads per bin,
divide by ENBW for density). Volts per code use the calibration chain of protocol section 8.5, probe factor included
[U: T8]. Number formatting is host-tested (`host_tests/test_spec_fmt.c`). Settings live in RAM (defaults: CH1, 2048,
Hann, 10 dB/div, ref 0 dBV, full span). Protocol: GET_STATUS byte 43 = display mode, READ_SPECTRUM 0x020A returns
the displayed spectrum (protocol section 14 row 45).

Costs: FFT per channel per new capture. With the MMU on (0.2.48, FFT_BENCH 2026-10-19): 1024 points 0.21 ms, 2048 points
0.68 ms, 4096 points 2.72 ms (the dB conversion of about 1-2.5 ms was measured before the MMU and is not re-measured).
Before the MMU (0.2.31): 2048 points 4.9 ms from DDR, 2.3 ms from on-chip SRAM, 4096 points 10.9 ms. Since 0.2.33 the
view runs N <= 2048 from SRAM A (0x1000-0x7008, the space FFT_BENCH borrows; nothing else uses it at run time, all
stacks are in DDR); with the cache on this placement no longer matters. The scope view's whole main-loop pass
(GET_STATUS `pass_ms`) went from 30 ms to a median of 15 ms (range 14-18) with the MMU. Drawing: one
728 x 432 clear (memset bursts since 0.2.32) and blit per pass, as 1.00o5's trace view.

Redraws in the spectrum view (fw 0.2.44): the only 1.00o5 routine that paints the scope view outside
`scope_display_trace_data` is `scope_preset_values` (host SET_TIMEBASE, the UI time base change, RUN in roll mode, the
spectrum's own span / Auto steps through `pc_hal_timebase_write`). It cleared the trace area and painted grid, cursors and
pointers over the spectrum, strip included, until the next pass. It now starts with `PC_SPECTRUM_DISPLAY()`: in the
spectrum view it redraws the spectrum and returns (trace buffers kept). `spectrum_display` has a re-entry guard (a time
base change from inside a draw redraws through the same path), and `spec_tb_write` (fw 0.2.77; `spec_tb_set` before) sets its wait state before the write, so
that redraw shows the new axis without the old spectra. Audit of the other host-driven redraws (SET_CHANNEL, SET_TRIGGER,
SET_GEN, RUN/STOP, the MCP chip): they draw only top-bar buttons (y < 46) and never touch the trace area or the right
column. Not host-testable (scope_functions.c and spectrum.c drawing are target-only); bench: spectrum view on, then
SET_TIMEBASE / SET_TRIGGER / SET_GEN in a loop from the host and span taps: the cards row (the strip until 0.2.56)
must never show grid or pointers.

### Slider track overview (`slider_ov.c`, fw 0.2.73)

The slider row under the plot of the Scope, the Spectrum and the Frequency response (design and maintainer decisions:
`docs/plans/slider-overview.md`): a line at y 452 parts it from the plot; the track below is x 2..727 (726 columns, the
window's mapping) and y 453..471 (19 rows); the window is its two 1 px white edge lines over the track's height. The
track shows the whole of what the plot shows a part of, in the full colour between the edge lines and at half intensity
(CH1 0x808000, CH2 0x008080, phase 0x804080) outside them. No text in the track.

- **Scope**: the min / max envelope of each enabled channel over the whole record (CH2, then CH1 over it), on the plot's
  own scale (`scope_code_to_y`, y 49..448 squeezed into the 19 rows); a column shares its first sample with the one
  before, so steps join. Drawn every pass after the traces, from the record as drawn (averaged when Average is on). Not in
  roll mode, X-Y, or the slow-rate branch with 1.00o5's fixed 181 px window. Cost (computed from the GCC 10 code, not
  measured): about 0.75 ms per pass at 2 x 16382 samples (the sample loop about 0.5 ms, unrolled by four, about 7.5
  cycles a sample; columns and drawing about 0.25 ms), about 0.35 ms at 2 x 3000; the row of "~" it replaced cost about
  0.1 ms. Bench: GET_STATUS `pass_ms` against 0.2.71.
- **Spectrum**: the spectrum over the whole axis 0 .. min(fs/2, 25 MHz) from the bins the FFT already computed, the
  highest bin per column, on the plot's ref level and dB/div; Spectrum: as shown (averaged or held), both analysed
  channels; Waterfall / Density: the latest record of the history channel. Nothing while waiting for a record at a new
  rate. Under 0.1 ms.
- **Frequency response**: AUTO SET's 20-point overview (magnitude; the phase in Phase only) fitted to the rows, valid
  points joined, flagged points one grey pixel. Copied when the overview pass completes, shown when AUTO SET ends (any
  result), kept until the next AUTO SET or power-off; Restore default settings clears it; a cancelled AUTO SET keeps the
  previous one. During the overview pass the track fills in point by point.
- **Tap to jump**: a touch-down on the row (y 450 down) outside the window centres the window on it (kept on the track,
  `sov_jump`), then the drag goes on; on release each mode does what a drag release does (Scope: nothing more, the
  record is scrolled; Spectrum: panned; Frequency response: the sweep restarts at the new span, or one sweep runs when
  stopped). Ignored during AUTO SET in Frequency response.

### Phosphor view (`phosphor.c`, fw 0.2.46)

Digital phosphor (intensity-graded persistence), like the DPOS350P's "digital fluorescent" display: the traces show
how often each pixel was hit by recent records, so jitter, rare glitches, a modulation envelope or the two duty
cycles of a toggling signal are visible at once with their relative frequency. Design and cost arithmetic:
`docs/plans/phosphor-design.md`.

Switched by the Phosphor switch in the CTRL page's Display fold-out (fw 0.2.56; the acquisition menu's Display row in
0.2.51, the main menu's "Phosphor display" row and the strip's "Scope" field before); it combines with Normal or X-Y.
The spectrum and phosphor views exclude each other; the switch stays on while the spectrum shows, so the main menu's
Scope row brings the picture back. Not available in roll mode or in the picture / waveform viewers (the picture pauses
and comes back when they end). It is a main-loop display mode: RUN/STOP, trigger, time base, channel menus, cursors,
measurements and the MCP keep working as in the scope view.

What changes: only the CH1 / CH2 traces. Each channel has a map of hit counts (one byte per pixel of the 728 x 400
trace area); every new record adds its line to the map (all samples on screen, each column from its lowest to its
highest point and joined to the next, so steep edges and dense records are drawn like peak detect), and the map fades
with the persistence time. Grid, cursors, cursor readouts, measurement tiles, the trace / trigger pointers and REF /
MATH traces are drawn as in the scope view, on top of the map. Where both channels light the same pixel the brighter
one is shown (CH1 on ties). XY mode works the same way with one map (CH1 against CH2).

Parameters, in the Display fold-out under the Phosphor switch since fw 0.2.56 (a strip over the record-position slider
before; the slider shows again):

| row | values | tap |
|---|---|---|
| Persistence | 0.2 s, 1 s (default), 5 s, Infinite | next value; clears the picture |
| Palette | Mono (channel colour, brightness by hit rate), Heat (dark blue, blue, cyan, green, yellow, red, white) | toggles |
| Clear | | clears the picture |

The Phosphor row's second line shows the records added per second ("28 wfm/s"; at the strip's right end before 0.2.56).
Warnings (the scope view's warning line at the top middle of the plot since fw 0.2.56, "Screens" above) while they apply: "CHn overrange:
increase V/div" (the latest record clipped), "No trigger: check the trigger level" (the latest record was an auto
time-out), "No channel enabled: switch on CH1 or CH2".

Reading the picture:
- Persistence is the time a pixel that is no longer hit takes to fade from full to 1/16 brightness. Infinite never
  fades: each hit counts 1, up to 255.
- Brightness means hit rate, not record rate: the increment per hit is scaled to the measured record interval, so a
  pixel hit by every record is at full brightness at any time base, and one hit by 10 % of the records at about half
  (square-root curve). In Infinite a single hit shows at about a quarter brightness.
- The picture clears by itself when what a pixel means changes: per channel on enable, coupling, V/div, trace position
  and DC offset (and recalibration); both channels on time base, sample rate, trigger position, memory depth,
  acquisition Filter, XY mode and persistence. It does not clear on trigger level or palette changes.
- Stopped: the picture freezes (no fading, persistence counts acquisition time). A modal menu also pauses the fade.
- Untriggered records from the firmware auto time-out are left out while a triggered one came within the last second,
  so a periodic signal's occasional miss does not paint a ghost that looks like a glitch.
- Records: every UI capture, and a host single capture (ARM) once when it completes; READ_TRACE re-reads do not add.

Limits:
- One record per main-loop pass (about 30 per second at the default 3000 samples), as in the scope view. This is not
  the DPOS350P's 50,000 wfm/s: rare events need time to show (a 1-in-1000 event needs about half a minute in
  Infinite). The fast loop of the design study (window readout, several records per frame) is a later step.
- Saving a waveform in the view keeps the thumbnail of the last scope-view trace (the waveform data itself is saved
  as usual); a screenshot (picture save, READ_SCREEN) shows the graded picture.
- The Trace selector (Linear / Dots / Peak) does not apply in the view: records are always drawn as joined
  min/max columns.

Settings: persistence and palette are saved with everything else at power-off (settings word 210, 0x9A00 marker;
restored only when the marker and the fields are valid). The view itself is not restored: the scope boots into the
scope view. Protocol: GET_STATUS byte 43 `display_mode` = 4 while it is on (protocol section 14 row 52); no new command.

Costs (estimates, arithmetic in the design doc): per record about 0.9 ms per channel for a normal trace (one pass over
the samples, one run per column, word accesses), per pass the lit area only (zero words skipped, grid shows through),
typically 1-4 ms on top of the scope view's pass (30 ms estimated before the MMU, median 15 ms measured since 0.2.48); worst case (the whole area lit on both channels) about 90 ms
per pass. Memory: 582 KB of maps in `.bss`; the code-to-row tables and palettes in on-chip SRAM A at 0x0400-0x0FFF.
Bench check: GET_STATUS `pass_ms` with the view on vs off.

### Frequency response (`bode_view.c`, fw 0.2.66 as "Bode plot", redesigned in fw 0.2.69)

Frequency response of a passive two-port (a filter, a divider, a tank) measured on the scope alone: the calibration
post's DDS square drives the DUT, CH1 reads its input, CH2 its output, and the view plots H = CH2 / CH1 as magnitude and
phase over a frequency axis. The same measurement the host tool `scope_bode_sweep` / `fnirsi1013d-bode` makes
(accepted on the bench 2026-10-19, docs/hardware-status.md 14:35-15:25), with the same frequency plan and the same
per-point steps, here as a main-loop state machine. Design: `docs/plans/bode-design.md` (the engine, steps D1-D4) and
`docs/plans/bode-ui.md` (the screen, maintainer's decisions of 2026-10-19 evening; its mockups are the acceptance reference);
protocol: `docs/protocol.md` section 14 row 60. "Bode" stays the internal name (files, BODE_CTRL, READ_BODE).

**Wiring** (the user guide's part; the screen says only what to connect and what to press): calibration post to the DUT
input, CH1 tip on the DUT input, CH2 tip on the DUT output, the DUT ground on the probe ground clips (wiring A, the bench
default since 2026-10-19). The post is a 3.1 Vpp square through 1 kOhm (R21); H = CH2 / CH1 does not depend on that
source as long as CH1 touches the DUT input. Passive DUTs or buffered inputs only: the 1 kOhm is the post's only
protection (never a coil, a MOSFET drain or anything that can drive the post). With the DUT ground on the USB shell
instead (wiring B) the DUT sits 1.24 V below the probe reference and saturates CH2 on 0.1 and 0.2 V/div: CH2 range Auto
steps up by itself (flag ch2_overload), Hold stops with a warning.

**Entering and leaving.** The main menu's third row, "Frequency" over "response" (fw 0.2.69; "Bode plot" in 0.2.68, the
generator menu's fourth row in 0.2.66-0.2.67), with a low-pass response icon. The Scope or Spectrum analyser row leaves
it. The row is grey on bitstreams without the filter and DDS ("Frequency response needs FPGA 0x1534 or later" at the top
of the plot when tapped) and in the waveform viewer ("Leave the waveform viewer first"). On entry the view takes the
generator, both channels (enabled, DC coupled, ranges and offsets of the sweep), the time base (raw dividers) and
RUN / STOP; an EXT trigger is set to CH1 (the pin is the generator's) with a note; from roll mode, roll goes off with
"Roll off for the frequency response". Leaving gives every one of them back as it was. Nothing is driven until a sweep
starts: entering shows STOP and, until the first point, one orange instruction at the top middle:
"Connect the calibration post and CH1 to the input, CH2 to the output, / grounds on the probe clips. Press RUN."

**Screen** (`bode_ui.h` has the geometry): one pane y 60..396, six divisions of 56 px shared by two y axes: magnitude in
dB on the left (labels in CH2 cyan, the top one "10 dB"), phase in degrees on the right (labels in pink, "+135 deg"); no
legend, each curve in its axis colour. Magnitude scales at 5, 10 or 20 dB per division, phase at 45 or 90 deg, both
chosen from the data so that the top division stays free (the warning line lives there) whenever the data then still
fits. The frequency axis runs from Start to Stop: Log (labels "100", "1k") or Linear (1-2-5 steps of the span; display
only, the points stay log spaced). Grid lines (fw 0.2.71, the grid rule of docs/ui-guidelines.md section 3, `bu_fgrid`):
horizontal at the labelled divisions; Log: every decade (dense dots, labelled), the 2 and 5 lines (sparse dots) when the
span is under three decades, labelled under 1.5 decades, and the linear steps when fewer than two such lines fall inside
(a tight span around a tank); Linear: every step, labelled (every other one when more than six). Trace style (fw 0.2.71):
Lines joins the valid points of each curve, Dots leaves them loose; flagged points are grey loose dots either way. White
triangles mark the -3 dB crossings, the peak of a resonance and the phase crossings at +-45, +-90, -135 and -180 deg,
from a complete table only (half a table misleads: a tank's rising skirt reads as a high-pass corner). While sweeping a
dotted grey line marks the point being measured. Cards at y 426, chosen through MEASURES; the slider row at y 452
(since 0.2.73 with AUTO SET's overview in the track and tap to jump; below, "Slider track overview").

**Right bar, first page** (the fixed meanings):

| slot | here |
|---|---|
| RUN/STOP | RUN: sweeps repeat, each overwriting the table point by point. STOP: the sweep ends after the capture in progress (fw 0.2.71; after the whole point before), the points stay. A single sweep (Single sweep, a host BODE_CTRL run) shows STOP on the button and RUN on the chip while it runs (never WAIT, whatever the trigger mode) and its cards read "sweeping" until it ends; a tap on STOP ends it the same way |
| AUTO SET | an overview sweep (100 Hz .. 10 MHz, 20 points, Settle Fast, CH2 Auto, the thru off for it; "Auto..." and the Sweep card "auto n/20"), then Start / Stop / Settle / CH2 range Auto from it (`bode_auto_span`), then one single sweep at them and STOP (fw 0.2.86, maintainer: AUTO SET never ends in RUN; "Auto..." until that sweep ends, a second tap, STOP or "Stop sweep" end it as a single sweep and the table keeps what it has; RUN is the user's explicit repeat). A band the overview cannot resolve (fw 0.2.71, the sharp-tank rule) is measured again around f0 first: f0 / sqrt(10) .. f0 sqrt(10) (Settle Resonant), f0 / 1.1 .. f0 x 1.1 and f0 / 1.03 .. f0 x 1.03 (High Q), 20 points each, until resolved (`bode_auto_zoom`). A second tap, or STOP, cancels: the previous table comes back (fw 0.2.71), "Auto set cancelled: settings kept". Flat: "No corner from 100 Hz to 10 MHz: span kept", one sweep at the kept span, STOP. fw 0.2.84: AUTO SET never loops (maintainer): an absurd point (more than 20 dB from both neighbours, a bad record) is dropped and the reference is the median of the three lowest points; an inconsistent pass ends it in STOP with the settings kept and "Auto set: no consistent response. Check wiring", "Auto set: no response: CH2 tip on the DUT output?", "Auto set: CH1 level low: CH1 tip on the DUT input?" or "Auto set: peak not confirmed: sweep by hand" (docs/plans/bode-ui.md 3.2) |
| T CURSOR | cursors C1 and C2: dotted lines with flags above the pane, placed at a third and two thirds of the axis, snapped to measured points (never interpolated). Drag a flag to move its cursor; a tap or drag in the plot moves the nearer one. Move fast sets the drag step (fw 0.2.71): fast, the cursor follows the finger; slow, it moves a fifth of the finger's travel. The readout box at the bottom left (x 10..261, y 342..393, over the response, maintainer): C1, C2 and C2-C1, f white, level cyan, phase pink; a flagged point shows its reason (unsettled, near floor, CH2 clipped, overload, ...) instead of the phase and its row and the difference turn grey |
| V CURSOR | empty (no level cursor in this mode) |
| MEASURES | "Frequency response measurements": Select All, Clear, Level, -3dB, BW, Peak, Q, Ph45, Ph90, Ph180, Sweep, Thru. Defaults -3dB, Peak, Q, Level (maintainer). A change shows at once (fw 0.2.71: the cards left of the open menu follow each tap, all of them when it closes). Magnitude cards on a cyan block, phase cards pink, Sweep and Thru neutral grey. "no sweep" before any, "sweeping" while a new table fills, "not found" when the marker is not there; Thru "applied" green, "stale" amber, "off" / "none" grey. More than six chosen: "+N more" in the sixth place |
| SAVE PIC | screenshot |
| SAVE WAVE | the sweep table as `bode/FR00001.CSV` (the next free number): `#` lines with the firmware, the bitstream, Start, Stop, Points, Settle, CH2 range, thru, elapsed ms, then `f_hz,mag_db,phase_deg,v1_db,v2_db,range1,range2,flags` per measured point (READ_BODE's fields, flags as the host's names joined by `|`). Without points: "Nothing to save: run a sweep first". Saved waveforms does not list these files; read them on a PC (USB drive) |

**CTRL page**: Display (y 65), Sweep (y 125), Thru (y 185), Trace style (y 245, fw 0.2.71), Grid (y 305; lower than in
the other modes, accepted by the maintainer); the rest of the stepper and the 50 % TRIG slots stay empty (the sweep ranges both
channels itself).

| button | fold-out rows | values |
|---|---|---|
| Display ("Overlay", "Mag", "Phase") | Overlaid, Magnitude only, Phase only (icons) | the chart: both curves and axes, or one of them |
| | Frequency axis | Log (default) / Linear, a tap toggles |
| Sweep ("50 pt") | Start (- / +) | 1 Hz .. 5 MHz in 1-2-5 steps (default 100 Hz) |
| | Stop (- / +) | 10 Hz .. 10 MHz (default 1 MHz); Start stays below Stop |
| | Points | 10, 20, 50 (default), 100: log spaced, merged where two round to the same DDS period |
| | Settle | Fast 3, Normal 10 (default), Resonant 50, High Q 200 periods before each record |
| | CH2 range | Auto (down-ranging with the splice) or Hold (CH2's range when the sweep starts: set it in the CH2 menu) |
| | Single sweep ("about 3 s", the estimate) | the panel stays open (fw 0.2.84) and the button reads what a tap does: stopped, green "Single sweep" / "about 3 s" runs one sweep; while RUN repeats, green "STOP after this sweep" makes the sweep in progress the last; during a single sweep, red "Stop sweep" / "n / N" stops it after the capture in progress; grey during AUTO SET's overview and passes, red "Stop sweep" during its final sweep (fw 0.2.86) |
| Thru ("None", "On", "Off", "Stale", "Meas.") | Measure thru | one sweep with both probe tips on the DUT input; stored (RAM) and applied |
| | Apply (switch) | divide the thru out of the sweeps (second line: matches this sweep / other sweep settings / none measured) |
| | Clear | forget it |
| Trace style ("Lines", "Dots"; the Scope's construction) | Lines, Dots | Lines (default): the valid points of each curve joined; Dots: loose. Flagged points are grey loose dots in both |
| Grid | | the grid dots, as in the other modes |

**Slider row** (maintainer, the Scope's construction at y 452..471): the track is 1 Hz .. 10 MHz on a log scale, the window
Start..Stop (the span of AUTO SET's pass while it runs). Dragging moves Start and Stop together by whole 1-2-5 steps, both kept
on the track; the axis follows and points inside the new span stay drawn. On release a sweep in progress restarts at the
new span (the point in progress dropped, the table cleared; RUN keeps repeating), and while stopped one sweep runs there.
The host's exact settings stop applying, a thru for the old span turns stale. Ignored during AUTO SET's overview.

**Warnings** (top middle, one at a time, at most 50 characters, consequence then action): a stopped sweep's reason ("CH2
overloaded by DC: set CH2 range Auto", "Sweep stopped: no capture started; tap RUN", "... no capture arrived; tap RUN",
"Sweep stopped: the PC took over", "No frequency in this range: change Start or Stop"), "Thru sweep: both probe tips on
the DUT input", AUTO SET's results, "CH2 overrange: set CH2 range Auto or raise V/div", CH1 level low by its cause
(fw 0.2.84: "CH1 level low at 2.5 V/div: sweep again" when CH1 was ranged coarser than 0.5 V/div, "CH1 level low: the
DUT loads the 1 kOhm source" at or below 2 MHz, "CH1 level low above 2 MHz: the x1 probe rolls off"), "N points not settled: choose a longer Settle", "N points near the floor: shown grey", "Thru does not
match this sweep: measure again", "DC level beyond the offset range: AC-couple DUT", "ADC readout fault: see
Diagnostics" (fw 0.2.71, READ_BODE reason 8: the pre-scan reached 5 V/div with a channel still clipping and its
fundamental under 20 codes, which the post's 3.1 Vpp cannot do; the maintainer's fail-fast rule, so a capture fault ends the
sweep instead of producing a table of flagged points), and the 5 s notes (EXT, roll).

**Top bar in this mode** (fw 0.2.71, bode-ui.md section 7): the ACQ chip reads "Set by" over "the sweep" and its dropdown
is one line, "The sweep sets the sample rate for each point"; while a sweep runs the generator chip shows the tone
being measured (duty 50 %, green edge; with the plot's redraw, at most every 150 ms; the background captures' generator-off
moments are not shown) and a tap on it shows "Driven by the frequency response sweep" instead of the menu (stopped, the
chip and the menu are the user's generator); the trigger menu adds a grey "Not used by the sweep" at its bottom.

**A sweep** (bode_sweep.c, the host tool's runner): the plan (f = 50 MHz / M; CIC3 + FIR at N = 16 x 2^k below 1.5 MHz with
257..512 samples per period below 24 kHz, legacy 200 MSa/s above; a coherent record of at least 8 periods and 2048
samples), a pre-scan at the first frequency on range 1 that picks both ranges (85 % of full scale) and centres the
offsets (up to four checks), then per point: a background capture with the generator off once per rate and CH2 range
(the near-floor flag), the settle time (three times the preset at the first point and after the generator was off),
the capture (FPGA auto, pretrigger 0), the single-bin DFT of both channels at the exact DDS frequency, re-centring when a
record's mean is more than 12 codes off, one retry with twice the settle when the two record halves disagree by more
than 0.1 dB / 1 deg beyond noise, the flags, and CH2 auto-ranging with the splice. Estimated durations (BODE_CTRL
answers the estimate; about 15 ms per capture assumed, to be measured): 30 points 1 kHz - 300 kHz about 1.2 s, 50 points
100 Hz - 1 MHz about 3.7 s, 50 points 10 Hz - 100 kHz about 27 s (the lowest decade's periods and its background capture
dominate).

**AUTO SET's span** (`bode_auto_span`, bode_sweep.c, host_tests/test_bode_sweep.c): a low- or high-pass with a -3 dB
crossing fc gets fc / 10 .. 10 fc; a resonance (or a phase through both +45 and -45 deg) f0 / 10 .. 10 f0 for Q <= 2,
half a decade either side for Q <= 10, f0 (1 -+ 4 / Q) above, Settle Normal for Q <= 7, Resonant for Q <= 34, High Q
above; a band narrower than two point spacings is not resolved (its Q reads low): its Q is taken as at least
1 / (2 ln r) for points r apart and at least 2, Settle one preset up, and AUTO SET measures it again on finer passes
(fw 0.2.71, `bode_auto_zoom`: f0 / sqrt(10) .. f0 sqrt(10), f0 / 1.1 .. f0 x 1.1, f0 / 1.03 .. f0 x 1.03, 20 points each;
on the model a Q 50 tank at 15.3 kHz is resolved on the third pass as Q 46 -> 10 kHz .. 20 kHz, High Q; Q 120 on the
fourth); a response still changing at 100 Hz moves Start to 10 Hz. The span is snapped outwards to
the 1-2-5 steps. On the DUT model: RC 1600 Ohm / 10 nF -> 500 Hz .. 100 kHz; the maintainer's 1 kOhm / 15 nF -> 1 kHz ..
200 kHz; the 991 / 102 Ohm divider: flat, span kept; a Q 8.1 tank at 50.3 kHz -> 10 kHz .. 200 kHz, Settle Resonant.

**Flags** (READ_BODE bits; grey on the screen when invalid): ch1_clipped, clipped (CH2), ch2_overload, low_snr,
near_floor, unsettled, incoherent make a point invalid; ch1_low, ch2_small, spliced, fir_off, retried, and with the thru
no_cal, cal_range, cal_flagged are informative.

**Host.** BODE_CTRL starts, stops and configures sweeps and the thru calibration and opens / closes the view; READ_BODE
reads the table (|H|, phase, V1, V2, ranges, flags per point, the M of each point). GET_STATUS byte 43 = 5 (Frequency
response) while the view shows; the features word's bit 6 says the bitstream can do it. A host sweep ends an AUTO SET
overview. Protocol unchanged in 0.2.69 (the CSV and the cursors are screen-only). fw 0.2.71: reason 8 (the readout
fault), and before the first sweep since boot the header's settings fields are the settings in effect, the same ones the
estimate describes (docs/protocol.md row 60).

**Settings**: Start, Stop, Points (word 219, 0xB000), Settle, CH2 range, the Display variant (bits 3..4), the cursors
(bit 5) and the frequency axis (bit 6) (word 220, 0xB100), the MEASURES list (words 213..215, 0xE000 marker in 213,
4-bit ranks) and the Trace style (fw 0.2.71, word 216, 0xB200 | bit 0) are saved at power-off; Restore default settings resets them. The view itself is not restored (the scope
boots into the scope view), and the thru calibration is not saved.

**Files**: `bode_dsp.c/.h` (planner, DFT, levels, CORDIC, markers; host_tests/test_bode.py against the host tool and the
plan vectors), `bode_sweep.c/.h` (the engine behind a `bode_hw` table, the READ_BODE codec, the Start / Stop steps and
`bode_auto_span`; host_tests/test_bode_sweep.c on a DUT model), `bode_ui.c/.h` (the screen as data: scales, axes,
readout rows, the MEASURES list and card texts, the texts, the slider maths; host_tests/test_ui.c with the device fonts),
`bode_view.c/.h` (the mode, drawing, touch, CTRL page, CSV, BODE_CTRL actions), `pc_bode.c` (the scope's `bode_hw`: raw
divider, the arm with this capture's filter configuration, the 1.00o5 readout and its scales). Hooks: `PC_BODE_PASS` at
the top of `scope_acquire_trace_data`, the main menu's row (`pc_glue_bode_available`, `pc_glue_main_menu_mode` opens and
closes the view), the spectrum glue (`pc_glue_spectrum_display / touch`, `pc_glue_autoset`) asks the view first,
`ctrl_page_draw / touch` hand the page over, `pc_right_bar_follow` follows it; statemachine.c's T CURSOR and SAVE WAVE
call `bode_view_tcursor` / `bode_view_save`, menu.c's MEASURES menu takes `UI_MS_BODE`.

**Limits / not built**: CH2 is always DC coupled (the host tool's `ch2_coupling='ac'` for wiring B is not on the device);
no harmonic fast mode and no impedance mode (bode-design D5); the screen's markers interpolate linearly in log f (the
host: a monotone cubic; they agree within the point spacing); the noise estimate is the median of period differences (the
host: the median over FFT bins), so a transient lasting most of the record reads low_snr rather than unsettled; Diagnostics
stops a running sweep. Not yet: a cancelled AUTO SET leaves the partial overview on
the screen (the settings are kept; the previous table is not restored); Move fast does not change the cursors' drag step
(they snap to measured points).

### Firmware auto trigger mode (fw 0.2.41)

The FPGA's auto time-out (0x0E) counts samples, i.e. one record length: 0.3 ms at 10 MS/s with 3000 samples, so a
1 kHz signal's edge often came too late and the record was untriggered; worse, 0x18 kept watching after the stop
and read "triggered" if an edge came before the MCU read it (flat records flagged triggered; the spectrum view
flickered between the comb and the floor). On Atlan bitstreams auto mode is now done in firmware like a real scope:
the UI acquisition and host ARM in auto arm the FPGA in normal mode, and after max(100 ms, two record lengths) without
a trigger the main loop writes 0x1A = 0, which ends the sampling at once with an untriggered record that is reported
as such (`pc_hal_scope.c` "Firmware auto mode", `pc_cmds.c` `pc_auto_deadline_ms`, host test test_fw_auto_timeout).
Calibration and auto setup still use the FPGA's auto mode (`pc_glue_decim_conversion` writes 0x1A per caller). The
spectrum view drops an untriggered record while a triggered one came within the last second (a periodic signal's
occasional miss) and otherwise shows it with the warning "untriggered (auto time-out)".

### Pass time in the acquisition menu (fw 0.2.38)

Maintainer: the cost of a longer record shows where it is chosen. In the acquisition menu's Memory column, under its rule
(since 0.2.63; between the "Memory" header and the first depth chip before, where it collided with both), the menu
shows "Pass time" and "NN ms", the main-loop pass time (GET_STATUS `pass_ms`) of the last pass before the menu opened. The menu
is modal and nothing captures while it is open, so a newly chosen depth shows its cost the next time the menu opens.
Drawn by the `PC_MASK_ACQ_MEMORY` glue after every memory-depth draw (`pc_hal_scope.c`).

### Memory budget

All new buffers are static in `.bss`, in DDR after the image. The linker script has one 32 MB
region from 0x7FFFFFE0, with `.text`, `.data` (with `.rodata`) and `.bss` placed in that order.
`start.s` puts the mode stacks at 0x81FB5000-0x82000000. Addresses below are from the GCC 13 build
of 5879cf2. They move with code size.

| item | size | address |
|---|---|---|
| `pc_tx_stage`, TX staging buffer: 16 + 66,560 + 4 bytes, rounded to 4 | 66,580 B | 0x8057F50C |
| `pc_rx_ring`, RX ring | 2,048 B | 0x8057ED04 |
| `pc_shadow_scope` + `pc_shadow_fpga`, UI snapshot | removed in 0.2.0 | |
| `pc_sess`, parser, duplicate cache, state | 1,676 B | 0x805903F4 |
| CRC-32 table, dispatch table (`.data`) | 1,024 + 312 B | in the image |
| `usb_tx` 32 KiB text ring | removed | |
| trace buffers, 2 x 16,380 to 16,384 B | +8 B | |

| (fw 0.2.x, history) | `PC_PROTOCOL=0` | `PC_PROTOCOL=1` |
|---|---|---|
| image (GCC 13 / GCC 10) | 348,672 / 346,112 B | 378,880 / 376,320 B |
| end of `.bss` (GCC 13) | 0x8057F738 | 0x80590A81 |
| free DDR up to the stacks at 0x81FB5000 | 26.2 MiB | 26.1 MiB |

Since fw 0.2.48 the MMU is on (docs/plans/mmu-dcache.md): the page tables (16 KB first level + 2 KB coarse tables,
`pc_mmu.c`) sit in `.bss`, and the 1 MB section at 0x81A00000 (restart record) is mapped strongly ordered, so nothing
else may live there (the linker script asserts that `.bss` ends below it). New DDR buffers are cacheable write-back
by default; a buffer that another bus master reads (only the display engine's `maindisplaybuffer` today) needs its
own mapping in `pc_mmu_build`.

The next fixed DDR user above `.bss` is the bootloader's copy of SD sector 710 at 0x81BFFC00,
22.4 MiB above the new end of `.bss`. The image needed 740 SD sectors from sector 750 then (1512 on
0.2.101, ending at 2261); it must end below the FAT partition, which starts at 16384 or later
(section 3).

### Bench loop without the boot menu (ENTER_FEL)

ENTER_FEL (0x0F01, magic 0x4C45465F) reboots the running scope into the boot ROM's FEL mode. The scope
answers OK, shows "FEL mode", detaches from USB, and about 0.5 s later the boot ROM enumerates as
`1f3a:efe8`. DRAM stays initialised, so a new image can be loaded directly:

```
<host> ENTER_FEL                                      # mcp client / any protocol host
usbipd attach --wsl --hardware-id 1f3a:efe8           # Windows: hand the FEL device to WSL
sunxi-fel version                                     # AWUSBFEX soc=00001663(F1C100s)
sunxi-fel -p write 0x7FFFFFE0 build-gcc10-pc/fnirsi_1013d.bin exe 0x80000000
```

Caveats: settings are not saved on the way into FEL. The boot ROM's expectations when entered this
way are inferred from Atlan's bootloader (SVC mode, IRQs off, SP 0x7C00, high vectors) and from a cold
FEL boot (USB controller in reset); the first ENTER_FEL on the bench is the test. If the scope hangs
on the "FEL mode" screen without `1f3a:efe8` appearing, power-cycle and use the boot menu.

Recovery without a power cycle: the text line `#RESET` (Enter) drops a response in flight, a pending
WAIT and partial input, and answers `#OK`. A response that makes no progress for 2 s is dropped
automatically, and reopening the port (DTR 0 -> 1) discards an old frame.

Boot: USB enumerates about 1.5 s before the main loop starts (GET_LINK_STATS counter 27: 1515-1517 ms on the
bench up to 0.2.16, first pass 31 ms). Up to 0.2.16 a card without `scope.bmp` (the normal case) made 1.00o5's
`load_picture` show "Failed to open file" in a box for 700 ms (or until a touch, with the confirmation setting on):
the brief error the maintainer saw at every boot. Since 0.2.17 the protocol build probes for the file silently and
skips the splash without a message; with the file present it is shown as in 1.00o5, for 3 s (counter 48 = the
time this step took + 1). The rest of the gap is the 1.00o5 init after USB (FPGA settings, main screen setup).
Meanwhile the timer poll answers PING, GET_INFO, GET_STATUS (`ui_busy` = 2 STARTING) and the other fast-path
commands; everything else waits for the main loop.

SD card FAT partition: the firmware needs nothing on it at boot. Optional `scope.bmp` in the root for a splash: it
must be exactly the scope's own screenshot format, an 800x480 16-bit RGB565 BMP of 768,070 bytes with 1.00o5's 70-byte
header (`bmpheader` in variables.c); the easiest source is a picture saved on the scope and renamed. It adds about
4 s to every boot (1 s read, 3 s shown), so the release package leaves it out. The picture and waveform folders are
created by the firmware on the first save (`scope_load_thumbnail_file`, `f_mkdir`); a real file-system failure is
reported only there, in the file menus.

Round trips: PING, GET_INFO, GET_STATUS, GET_BATTERY, `#VER` and `#RESET` are answered from the
1 ms timer interrupt, so they take about 1 ms plus USB time in any state. Commands that touch the
FPGA, the UI settings or the display run on the main loop, between UI passes, so they wait for the
next pass (35-75 ms in RUN). GET_CAPTURE_INFO is answered from the timer interrupt too. GET_STATUS
(protocol 1.1) reports that pass time at offset 28 (last) and 30 (max since the previous GET_STATUS).
In RUN a READ_RAW waits up to 1 s for the UI capture in flight (protocol section 14, row 35).

Link counters: GET_LINK_STATS (0x0007, protocol 1.2) returns 31 u32 counters since boot (transport,
parser, dispatch and, since 0.2.0, four startup intervals; list in `docs/protocol.md` section 14). `host_tests/bench_link.py` prints them.
`rx_swept` counts request packets whose USB interrupt never arrived and that a poll picked up
instead; `link_resets` counts bus reset / suspend / disconnect events (each drops buffered data).
Counters 47 (`usb_irq_polled`) and 57 (`usb_irq_races`) are retired since 0.2.21 and always 0: they
belonged to the USB timer-tick fallback (0.2.12-0.2.20), removed because the 0.2.20 soak showed
1.00o5's interrupt dispatcher never loses a USB interrupt (the fallback only raced the real ISR). The
slots stay so decoders by count keep working; event type 14 is likewise retired.

### First hardware bring-up checklist

Order is by risk: each step is gated on the previous one. Load over FEL only, as in section 4A:
the image runs from DDR and the scope image on the SD card is untouched. One write still happens:
the power-off ISR saves settings to sector 709, as 1.00o5 always does. With `PC_FORCE_CDC=1` that
save stores "USB CH340 mode" = on.

The maintainer's 25Q80 now holds Atlan's AL3 bitstream (0x1532, variant 2; written and read back
2026-10-18, power-cycle check of the diagnostics screen pending). The expected values below are
for 0x1532. The stock bitstream (0x1432, variant 1) gives `fpga=0x1432`, ring 4096, caps 0x7C00,
and E_UNSUPPORTED for the generator and for geometry other than 1500/3000. The maintainer also saw
"Cannot recognize device" from the updater's MSC mode on Windows (cable suspected): rule that out
with a known-good cable before step 2.

```
make gcc10                                             # conservative compiler first
sunxi-fel -p write 0x7FFFFFE0 build-gcc10-pc/fnirsi_1013d.bin exe 0x80000000
```

1. **No host.** The scope boots to the normal UI with no MCP chip. Touch, menus, run/stop and
   calibration work. The USB menu still switches to MSC, the PC sees the SD card, and leaving the
   menu returns to CDC.
2. **Enumeration.** Windows Device Manager shows a COM port `USB\VID_1209&PID_0001` with usbser.sys
   and no driver install. On Linux, `lsusb` shows `1209:0001` and `/dev/ttyACM0` appears; add the
   ModemManager ignore rule from plan section 10. The device is high-speed only: use a USB 2.0 HS
   port or hub, not a full-speed VM pass-through.
3. **Text probe.** In a terminal, `#VER` + Enter must answer
   `#OK,FNIRSI-1013D-AI,fw=0.2.3,proto=1.3,fpga=0x1532`. `#FOO` must answer `#ERR,UNKNOWN`.
4. **Identify.** PING with random 0..492 byte payloads, 10,000 times. GET_INFO must report
   variant 2, ring 8192, max total 16382, caps 0x7C03 (PROG_TRIGGER_POS, GENERATOR_DDS, ENTER_FEL,
   SHARED_MODE, SD_SECTORS and the two UNVERIFIED bits; `PC_DEBUG=1` adds 0x120). Run GET_STATUS, GET_BATTERY,
   and GET_CHANNEL / TIMEBASE / TRIGGER / CAL, and compare with the screen. The UI keeps running, and
   the green MCP chip shows directly above the RUN/STOP chip while the port is open (steady, no
   pulse; the RTC clock is hidden meanwhile). Open a menu: GET_STATUS must
   answer with `ui_busy` = 1; SET_CHANNEL closes the menu (silently, no note) and is answered (fw 0.2.78;
   E_BUSY before).
5. **Link tests T1, T2.** TEST_PATTERN lengths 492, 495, 496, 1008 and 4076, 1000 times each,
   with no timeouts (ZLP). Then 65,536 bytes x 200: record MB/s in `docs/hardware-status.md`.
6. **Shared control (protocol 1.3, plan acceptance).** Change the timebase on the touch screen:
   GET_TIMEBASE shows it within one UI pass, and GET_STATUS `settings_seq` went up. SET_CHANNEL a
   range from the host: the screen shows the new V/div and the relay clicks once. In RUN, READ_TRACE
   returns the displayed trace. ARM + WAIT: the capture appears on screen and the run chip shows
   STOP; RUN resumes from the screen and from SET_ACQ_STATE 1. Change the range during an
   averaged_capture run: the tool reports the restart. The MCP chip goes away 2 s after the port is
   closed. Power off and boot again: the last settings, from either side, are kept.
7. **Readout with the UI's geometry.** Leave the trigger and memory settings as the UI had them.
   Use auto trigger mode with a signal on CH1. Run ARM, WAIT, READ_RAW (all cores, `addr_mode` 1,
   `total_words` words): repeated reads of one capture must be byte-identical. Compare READ_TRACE
   with the screen. With a `PC_DEBUG=1` image, run T3 (`read_lag`), T5 (trigger alignment) and T6
   (0x18 after an auto timeout).
8. **Hardware-changing commands, front end.** SET_CHANNEL across all ranges and both couplings:
   relays click, and the read-back equals the request. Then the offset, SET_TIMEBASE (index and
   raw), and SET_TRIGGER including geometry up to 16,382 samples. The screen follows every change.
   Run T8 with a known DC voltage on each range.
9. **Hardware-changing commands, output.** SET_GEN drives the calibration pin: check it with a
   probe first, then loop it back to CH1 (`self_test_loopback`). Run T4 (interleave order) and T7
   (FORCE, `PC_DEBUG=1`). T10 (stock trigger core) needs the stock bitstream restored from
   `backups/`. Only after T3, T4 and T7 pass may the firmware clear the UNVERIFIED bits or set
   FORCE_TRIGGER in a release build.

`BENCH: Tn` comments in `pc_cmds.c`, `pc_hal_scope.c`, `pc_transport.c` and `usb_interface.c` mark
the code each test confirms.

## 6. Calibration

### 6.1 What 1.00o5 measures, stores and applies (analysis, fw-cal 2026-10-18)

Sources: `scope_functions.c` (`scope_do_baseline_calibration`, `scope_do_channel_calibration`,
`scope_input_calibration`, `scope_load/save_input_calibration_data`, `scope_save_config_data`), `fpga_control.c`
(`fpga_read_sample_data`, `fpga_read_adc_data`, `fpga_set_channel_offset`), `statemachine.c` (the menu flow), bench
facts from `docs/hardware-status.md` (T8, the Nyquist spur). Line numbers drift; search the function names.

**Base ("baseline") calibration**, System settings, prompt "Please unplug the probe and USB first!". Trigger off
(samplemode 0, AUTO), trigger level 0, invert off, dcoffset 0, `calibrationsettings` (a scratch CHANNELSETTINGS)
with both compensations 0, 1000 samples per readout (500 per core) read from ring position 100. Per channel
(`scope_do_channel_calibration`), for each of the 6 hardware ranges (relay 0x33/0x36 value 0..5, 60 ms settle):

1. At 200 MSa/s (rate index 0) and 50 kSa/s (index 11): offset PWM 500, 30 ms, one conversion, integer mean of
   core A only (`adc1rawaverage = sum / 500`, truncated) = H; PWM 1200, same = L. Step (PWM counts per code, Q20)
   `s = (700 << 20) / (H - L)`; PWM estimate for A = 128 from each end, `500 + ((H - 128) s >> 20)` and
   `1200 - ((128 - L) s >> 20)`, averaged. The two rates' estimates are averaged again.
2. At 2 MSa/s (index 6), PWM = that estimate, 60 ms, one conversion: **FAIL 1** if core A's integer mean (no
   compensation) is outside 125..131, i.e. the linear PWM model missed 128 by more than 3 codes on any range (noise,
   a non-linear offset path, a wrong reference: thread notes, a 2.5 V TL432 instead of the 1.25 V part gives FAIL 1,
   research/notes/thread-pages-121-155.md). The integer difference B - A of the same readout is summed.
3. `d = sum / 6` (C division, truncates toward zero); `adc1compensation = d / 2` (truncated),
   `adc2compensation = -(d - adc1compensation)`, so the two add up to -d. **FAIL 2** if |d| > 20 codes.
4. Each range's PWM is moved by `adc1compensation x mean step` so that core A **plus its compensation** reads 128:
   that is `dc_calibration_offset[r]`. Entry 6 (50 mV/div, software zoom of range 5) copies entry 5.

The results are copied per channel only while the running flag is still 1 (a CH1 failure also discards CH2; a CH2
failure leaves CH1 updated), and are **not written by the calibration**: they live in RAM and reach sector 709 at
the next power-off (`scope_save_configuration_data`). Coupling is not set (whatever the channel had).
`calibrationsettings.dc_shift_value` is never initialised, so every calibration sample evaluates 0 / 0 in the
dcoffset term below (libgcc's `__aeabi_idiv0` returns 0).

**Input-divider ("voltage") calibration**, offered after a base calibration only while `calibrationfail` is set
(first run or after RESET): DC coupling, 1x, dcoffset 0, 5 MSa/s time base, one conversion per step of both
channels with 300 mV / 600 mV / 1.5 V / 3 V / 7.5 V / 15 V on ranges 5..0; `value = average - 128` (integer mean of
both compensated cores, the zero assumed at exactly 128), then `input_calibration[r]` counts up from 3,000,000 until
`((value x ic) >> 21) x mul_factor == calibVolt` (10 uV units, e.g. 30000 for 300 mV); failure above 4,000,000.
`input_calibration[6] = 2 x [5]`. Saved to sector 708 when the sequence completes. Any zero error goes straight
into the gain.

**DC shift parameters** (diagnostic menu, manual +/- trim, saved to 708 at "End"): `dc_shift_value` (default 385)
maps the offset PWM to codes for the measurements (`+ dcoffset x 100 / dc_shift_value` per sample, i.e. 0.26 codes
per count), `dc_shift_size` (220) maps it to screen pixels for the trace and the trigger marker, `dc_shift_center`
(112/174) is used by auto setup.

**Sector 708** (`INPUT_CALIBRATION_SECTOR`, little-endian, byte offsets):

| bytes | field |
|---|---|
| 0-3 | u16 checksum high, u16 checksum low: sum of the u16 words 8..59 (bytes 16..119). Valid only if high is 1..0xFFFE |
| 4-15 | 0 |
| 16-43 | CH1 `input_calibration[0..6]` (i32, 2^21 scale) |
| 44-47 | 0 |
| 48-75 | CH2 `input_calibration[0..6]` |
| 76-79 | 0 |
| 80, 84, 88 | CH1 `dc_shift_center` (u32), `dc_shift_size` (i32), `dc_shift_value` (i32) |
| 92-95 | 0 |
| 96, 100, 104 | CH2 the same |
| 108-511 | 0 (not covered by the checksum; every save rewrites the whole sector from zeros). Open firmware: 128-431 calibration v3 block (6.3), 432-487 calibration provenance (fw 0.2.52, protocol row 55: firmware build and version, FPGA word, what was written, uptime, power-on count and operating minutes at the calibration, DS3231 time when fitted; CRC-32), stamped when a save carries a changed v3 block |

**Sector 709** (settings, u16 words, checksum of words 2..255 in words 0-1, version 0x0100/0x0015 in words 2-3)
holds the base calibration results: words 168-173 CH1 `dc_calibration_offset[0..5]`, 174-179 CH2, 184-187
`adc1compensation` / `adc2compensation` of CH1 then CH2 (i16). They are taken from the loaded 709 buffer only
when sector 708's checksum is valid; otherwise defaults (PWM 860, compensation 0, `signal_adjusters`, 174/220/385)
load and the input calibration is offered again (fw 0.2.59: the first-run screen and the volts calibration on the
Diagnostics Calibration page). Clear calibration there sets the PWM and compensation defaults in RAM (1.00o5's RESET
without its volts-factor reset; RESTORE is gone). fw 0.2.52: words 224..255 hold the unit life record (power-on count, operating minutes, the
session-open flag, the restart record kept over power cycles; layout in protocol row 55, `pc_life.c`), stored by every
save; the firmware writes the sector once per boot when it loaded valid, so the count and a restart record reach the
card even when the power-on ends without the power-off save.

**Where the compensation is applied.** `fpga_read_adc_data` (every UI readout, READ_TRACE, the calibrations): per
core `sample = raw + compensation`, clamped to 0..255, overload flags, and in the second (core B) pass a clip-matching
rule that copies one core's value to the other near 0 and 255 so the two saturate at the same code. Then, for
min/max/average/RMS only (not the trace buffer), `+ dcoffset x 100 / dc_shift_value` (a soft-division per sample).
The decimation path (0x1534, K >= 1) adds `(adc1compensation + adc2compensation) / 2` to every 16-bit sample
converted to a code (`pc_glue_decim_read`). `fpga_average_trace_data(_long)` (test.c only) adds core A's value.
READ_RAW is raw.

**Why the per-core spread survives on the maintainer's unit.** CH1's raw core zeros differ by 5.35 codes (A 130.93,
B 125.58, live capture 2026-10-18; T8: Z_A 132.5 / Z_B 127.0 at dcoffset 0); the stored compensation is -2/+2, so
4 codes are corrected and 1.35 remain. The arithmetic loses up to about 2 codes: core means are truncated to
integers (each core loses 0..1 code, independently); the 6-range mean of B - A is truncated toward zero (a true
-4.9 becomes -4); the split is in whole codes. Inference, not verified: the base calibration on the card was run
on the stock bitstream (0x1432, hardware-status 2026-10-18), and the spread under the AL3 bitstream may differ.
Beyond the quantisation the model is too small: one compensation per channel for all ranges and offsets, while
the two cores' offset slopes differ (CH1 -0.2641 vs -0.2598 codes per count, host fit), i.e. another 0.4 codes per
100 counts of dcoffset. In legacy sample-skipping mode consecutive samples alternate A, B, A, B, so the residual
A - B difference is a square wave at fs/2: the Nyquist-bin spur in the spectrum view. The trace's 0 V also sits
at the mean of the two compensated cores, not at 128 - dcoffset x 100 / dc_shift_value, so the screen zero and the
voltage calibration's zero are off by up to a code.

**The legacy-vs-filtered step (bench 2026-10-18, 0x1534, CH1 on a steady level, 1 MSa/s): legacy mean 84.40,
boxcar 86.22, CIC2 86.20, CIC3 86.56.** Not firmware arithmetic: both paths weigh A and B equally (legacy reads 0x20
and 0x21 into alternate positions; the filter's taps split evenly between the cores for even N) and both add the
same mean compensation, (adc_a_comp + adc_b_comp) / 2 = 0 here. The cause is in the FPGA clocking: in legacy mode
(K = 0) `zaklad.v` gates the ADC encode clocks with the sample-rate divider (`sample_clock_enable` drives
`adc*_encA/B`), so at divider D each core is clocked at 100 MHz / (D + 1); with the filter on,
`sample_rate_divider_eff` is 0 and the ADCs always run at 100 MHz. The cores' zeros depend on the encode rate. The
bench session's READ_RAW sweep: 200 MSa/s A 87.80 / B 85.09; 20 MSa/s A 87.06 / B 81.84; 2 MSa/s to 1 kSa/s flat
(A 86.98-86.99, B 81.45-81.51). Core B drops 3.6 codes as soon as the clock is gated, core A 0.8, so the gated mean is
1.8-2.2 codes below the full-rate mean, and the filtered captures read the full-rate mean. The mechanism inside the
AD9288 is not known. Inference: it is not the datasheet minimum of 1 MSPS (research/notes/hack-repo-hardware-fpga.md),
since 20 MSa/s = 10 MHz per core already shows the full step. Below 2 MSa/s the legacy time bases do clock each core
under 1 MHz, outside that minimum, which is one more reason to make the filter modes the default.

### 6.2 Calibration data v2 (fw 0.2.43, protocol 1.10)

Superseded by v3 (section 6.3) for the block, the gains and the readout arithmetic; the zero model, the zero
step and its checks below still hold.

Files: `adc_cal.c/.h` (model, sector block, checks; pure C, host-tested by `host_tests/test_adc_cal.c` and the
`adc_cal` section of `docs/protocol-vectors.json`), `scope_functions.c` (`scope_do_baseline_calibration`, load and
save of sector 708), `fpga_control.c` (`fpga_channel_correction`, `fpga_prepare_correction`, the readout loop),
`pc_hal_scope.c` (16-bit path), `pc_cmds.c` (GET_CAL, GET_CHANNEL zero), `menu.c` (diagnostics, failure text).

**Model.** Per channel, hardware range r (0..5; range 6 uses 5), core k (A = 0x20/0x22, odd trace positions; B =
0x21/0x23, even) and clock class c (FULL = legacy divider 0 and every filtered capture; GATED = legacy divider >= 1):

    raw_k(0 V) = zero[c][k][r] / 16 + slope[k][r] / 65536 x dcoffset          (codes)

**Measurement** (`scope_do_baseline_calibration`, the existing prompts; inputs open or shorted). Trigger off, invert
and dcoffset 0, DC coupling forced (restored after), 500 samples per core per conversion. Per channel and range:
relay, 60 ms; at 2 MSa/s the mean of both cores at PWM 500 and 1200 (2 conversions each) gives the coarse centre
(reject if 700 counts move the mean by under 100 codes or the centre is outside 200..1800); means per core at
centre - 150 and centre + 150 (60 ms settle, 4 conversions each) give each core's slope and the refined centre P
where the mean of both cores reads 128; at P, 8 conversions gated (2 MSa/s) and 64 at full rate (200 MSa/s; a readout spans only 5 us of the
40 us PWM ripple period, so it takes many to average the ripple phase) give the
zeros (1/16 code), the RMS noise and the drift between the first and the second 4 conversions. About 0.4 s per range,
5 s in all. Checks per range (`acal_range_store`, first failure wins, shown as "CH1: <reason>" in the failure box;
nothing changes unless both channels pass): no sample at 0 or 255; noise <= 3 codes RMS at full rate, <= 1 code gated
(fw 0.2.100, maintainer 2026-10-20; 3 codes for both before; the Diagnostics limits); drift <= 0.5 code; slopes
-0.20..-0.33 codes per count and within 0.02 of each other; zeros within 16 codes of 128 and within 20 codes of
each other per class; full and gated zero of a core within 12 codes. On success: `dc_calibration_offset[r] = P`
([6] = [5]); `adc1/adc2compensation` = whole-code split (|A - B| and the mean error at most half a code) of the gated
zeros averaged over the ranges, so an older firmware with this card is better off too; then the v2 block to sector
708 and the settings to 709 at once (1.00o5 left the results in RAM until power-off).

**Sector 708 block** (bytes 128..455, outside Atlan's checksummed 16..119; an older firmware ignores it, and its own
save of 708 zeroes it): u32 'ACAL', u16 version 2, u16 length 328, u16 classes 2, u16 0, then per channel u16 pwm[6]
(the PWM centres it was measured at), i16 zero[class][core][range], i32 slope[core][range] (Q16), u16
noise[class][core][range] (1/16 code), then u32 CRC-32 (zlib) of the 324 bytes before. A channel uses it only while
pwm[0..5] equal its `dc_calibration_offset[0..5]` (from 709): an older firmware's base calibration, RESET or the
defaults fall back to the 1.00o5 compensations by themselves. RESET also clears it in RAM. No settings-sector words
are used.

**Correction per readout** (`acal_correction`, once per `fpga_read_sample_data`, divider = the last 0x0D value):

    shift   = trunc(dcoffset x 100 / dc_shift_value)       (what the measurements add, as in 1.00o5)
    T16     = 16 x (128 - shift)                            (0 V target, 1/16 code)
    off16_k = round(slope[k][r] x dcoffset / 4096)
    d16_k   = T16 - zero[c][k][r] - off16_k                 (clamped to +-127 codes)
    comp_k  = floor(d16_k / 16),  frac_k = d16_k - 16 comp_k
    code    = clamp(raw + comp_k + (u < frac_k), 0, 255),   u = top 4 bits of a xorshift32 per sample

so both cores' 0 V sits at T to 1/16 code on average. The dither adds f(1 - f) <= 0.25 code^2 of uncorrelated noise
(about 0.4 code RMS averaged over f), no tones. The measurements then add `shift` (min, max, average, RMS in the
128-based frame), and the screen draws the trace with 1.00o5's `dc_shift_size` mapping, whose 0 V marker
corresponds to T. GET_CHANNEL `zero_q8` = 256 x T. Without a bound block: the 1.00o5 compensations, no dither.

**16-bit filter path** (0x1534, K >= 1). Each output y sums an N^K-weighted window over consecutive samples, N even,
alternating cores, so the weights split N^K / 2 per core and the offset in y is N^K (Z_A + Z_B) / 2 with the
full-rate zeros (Z_k = zero[FULL][k][r] / 16 + slope term). Corrected y' = y + N^K (T - (Z_A + Z_B) / 2); in stored
units (>> S) the constant is N^K (T - (Z_A + Z_B) / 2) / 2^S. The firmware applies it after the conversion, in codes:
`corr16 = round(T16 - (zf16_A + zf16_B) / 2)`, `code = clamp(round(out x 2^S / N^K + corr16 / 16))`
(`pc_decim_to_code`, Q24), then takes comp_k off so the 8-bit loop's per-core add cancels (no dither on 16-bit
readouts). WIDE READ_TRACE stays raw; GET_CAL gives corr16 for the host. The spectrum view's 16-bit FFT
(fw-gen5, `fx_fft_run_u16`) subtracts 0 V in stored units, (16 T - corr16) x 2^20 / scale_q24, with the corr16 of
the record's readout.

**Cost in the readout loop** (`fpga_read_adc_data`, GCC 10 -O3, objdump of main vs this branch): the per-sample
`dcoffset x 100 / dc_shift_value` (two uncached struct loads and an `__aeabi_idiv` call) and the per-sample load of
the compensation are gone; added are 7 register instructions (xorshift 3, shift, compare, conditional add 2). With
uncached data accesses at about 90 cycles (README "FFT core") the pass should get faster, not slower; measured on the
bench with GET_STATUS pass times (list below).

**Bench checklist** (bench session and maintainer; FEL load of the 0.2.43 image, no SD write needed except the base
calibration's own 708/709 write: back up sectors 700-710 first):

1. Before: GET_CAL ch 0 and 1 give byte 72 = 1, flags 0; diagnostics show "Calibration data: v1". Note GET_STATUS
   pass times at 3000 samples, 20 us/div and 1 ms/div (30 s each).
2. Maintainer: probes and USB unplugged, System settings, Base calibration, OK. Expected "Ch1.... OK", "Ch2.... OK",
   "Calibration successful" after about 5 s. Reconnect USB.
3. GET_CAL: byte 72 = 2, flags 3 (7 on a gated time base), the stored block, and the diagnostics view
   (calibration data: v2, zeros, d, noise, slopes about -2640 / -2600 e-4 per count). Expected on the maintainer's CH1
   (shorted, dcoffset 0): gated A ~130.9 / B ~125.6 (T8, live capture), full rate higher by the bench session's
   READ_RAW sweep deltas (2026-10-18, steady level on the calibration post: 200 MSa/s A 87.80 / B 85.09, gated
   20 MSa/s..1 kSa/s A 86.98-87.06 / B 81.45-81.84, so full - gated = A +0.8, B +3.6): full A ~131.7 / B ~129.2;
   d gated ~5.3, full ~2.5. CH2: gated A ~128.4 / B ~128.9 (T8, within 0.5); its full-rate values are unmeasured.
   In the block the zeros are x 16 (e.g. 2094 / 2009 gated, 2107 / 2067 full for CH1).
4. Shorted inputs, filter off, READ_TRACE (8-bit) on every range at 200 MSa/s, 20 MSa/s, 2 MSa/s, 200 kSa/s,
   20 kSa/s, 1 kSa/s, dcoffset 0, -300, +300: per-core means (odd vs even samples) equal within 0.3 code and equal to
   zero_q8 / 256 within 0.3 code. READ_RAW of the same capture keeps the raw spread.
5. Same with the filter on (boxcar, CIC2, CIC3) at 1 MSa/s and 10 kSa/s: mean = zero_q8 / 256 within 0.3 code, so
   legacy and filtered agree (the 1.8-code step is gone).
6. AC and DC coupling on all 6 ranges, gated clock: shorted-input means within 0.3 code of zero_q8 / 256 in both
   (the calibration ran in DC; inference: the AC relay sits before the attenuator, so the zero is the same).
7. T8-style offset sweep: dcoffset -600..600 in steps of 100 on ranges 0, 3, 5: corrected per-core means track
   zero_q8 / 256 within 0.3 code (slope model).
8. Spectrum view, CH1 shorted, 1 MSa/s legacy: the fs/2 bin (was -32 dBV over a -50 dBV floor) at the floor.
9. Bench supply + DMM, 1x, DC: 1.000 V on range 3 and 0.010 V on range 5 (average 16 captures): scope average and
   host model within 1 % at 1 V; at 10 mV (about 3 codes) within the averaged noise.
10. Pass times as in step 1: equal or faster.
11. Old firmware with the new card (backup first): 0.2.42 boots and shows the new PWM centres and compensations;
    after 0.2.42 saves sector 708 (input or DC-shift calibration end), 0.2.43 reports flags 0 (block cleared).

### 6.3 Calibration data v3: per-core, per-class gain (fw 0.2.45, protocol 1.12)

Why: the gain bench of 2026-10-18 (docs/hardware-status.md, CH1 range 2, +-1.0045 V against the shorted-input raw
zeros) found a linear gain mismatch between the cores at full rate (A 28.62, B 27.71 codes/V, A/B 1.033) and almost
none gated (A 28.54, B 28.42, A/B 1.004), and the full-rate class 1.1 % below the gated one. The zero-only v2 model
left an A/B difference that grows with the signal (0.9 code per volt at 200 MSa/s, the fs/2 spur) and made 200 MSa/s
and every filtered capture read 2.7-3.5 % low against the gated voltage calibration. One gain per core and class is
enough (both polarities give the same ratio; core A's gated 0.5-code bow is INL and stays).

**Model.** raw_k = z_k + V g_k[c], z_k = zero[c][k][r] / 16 + slope[c][k][r] / 65536 x dcoffset (the offset slope is
now per class too). G_k[c] = g_ref / g_k[c], g_ref = the mean gain of both cores in the gated class, which is what
`input_calibration` is fitted on: legacy gated volts keep their calibration, the full class is pulled onto it.

**Gain step** (AL3 bitstreams only; since the calibration wizard, section 6.4, one channel at a time, step 2 of the
wizard; 0.2.45-0.2.48 asked for both probes in a box after the zero step). The generator is set to frequency 0 with
threshold 0 (output low, about -1.24 V against probe ground) or all ones (high, about +1.87 V); both cores and both
clock classes see the same node, so no level needs to be known. Per channel, about 0.7 s:

1. Range 2, PWM centre, gated: 2 conversions per level. The span picks the range (`acal_gain_range`, nominal
   sensitivities 1 : 2 : 5 : 10 : 25 : 50): the most sensitive with a predicted span of at most 120 codes. x1 probe:
   range 2, about 88 codes; x10: range 5.
2. The offset PWM is moved (`acal_gain_pwm`, gated slopes) so the two levels sit symmetric about the 0 V code: the
   midpoint of the levels is +0.31 V, so without it the levels sit at -35 / +53 codes, and core A's bow (0.53 code at
   +-28 codes, quadratic) would shorten its span by 1.2 % (0.5 % gain error; the host test shows 0.07 % centred).
3. Low, high, low again; at each 8 gated conversions (2 MSa/s) and 64 at 200 MSa/s (the 25 kHz PWM ripple, as the
   zero step). span_k[c] = high - mean of the lows (linear drift cancels); G_k[c] = (span_A + span_B)[gated] / 2 /
   span_k[c], stored as Q15.

Checks (`acal_gain_store`, first failure wins, "CH1: <reason>" in the box; a failed channel keeps its gains, the other
channel's new ones are saved): any sample at 0 or 255 "gain: level clips"; the two lows more than 0.5 code apart "gain:
level drifts"; any span under 40 codes (no probe, levels swapped) "gain: probe on post?"; |G_A - G_B| > 0.08 in a class
"gain: cores differ" (bench 0.033); (G_A + G_B) full / gated outside 0.9..1.1 "gain: clock classes" (bench 1.011). A
new zero step carries the previous block's measured gains over (`acal_gain_carry`, flag KEPT); never measured =
unity.

**Assumptions (to verify on the bench, list below).** (1) The gain mismatch is inside the ADC, so gains measured on
one range hold on all six (inference from the mechanism: the cores share the front end; not measured). (2) The post
levels are static for the second the step takes (logic level of the FPGA pin against VRF1). (3) INL is even about
the 0 V code to first order (only that part cancels by the centring).

**Correction** (`acal_correction`, once per readout): gain_k = G_k x 65536, off_k = round((65536 T16 - gain_k z16_k) /
16); per sample code = clamp((raw x gain_k + off_k + u) >> 16), u = the top 16 bits of the xorshift32, so the mean is
exactly T + G_k (raw - z_k). With v1 (no block) gain 65536 and off = compensation x 65536 give the 1.00o5 arithmetic
exactly. The rail rule (core B pass) uses clip_lo = the higher of both cores' codes for raw 0 and clip_hi = the lower
of their codes for raw 255: below / above them both codes of the pair take the lower / higher one (equal to the
1.00o5 rule with unity gains). 16-bit path: gain16 = 2 G_A G_B / (G_A + G_B) of the full class (each output weighs
both cores equally), folded into the conversion scale (`acal_scale`), corr16 = T16 - gain16 / 65536 x the mean
full-class zero; the 8-bit loop then runs at unity gain with no offset. The spectrum view's 16-bit FFT uses the same
scale for its zero and dB terms.

**Cost.** GCC 10 objdump of `fpga_read_adc_data`, 0.2.44 vs 0.2.45: the same loads and stores per sample; core A
pass 24 instructions per sample instead of 26 (mla + add + asr replace shift, compare, conditional add), core B pass
about 2 more ALU instructions for the min / max of the rail rule. Pass time expected unchanged (30 ms on 0.2.43).

**Block v3**, sector 708 bytes 128..431 (304 bytes): header as v2 with version 3, length 304; per channel 144 bytes:
u16 pwm[6], i16 zero[class][core][range], i16 slope[class][core][range], u8 noise[class][core][range], u16
gain[class][core] (Q15), u8 gain_range (0xFF never measured), u8 gain_flags (bit 0 kept), u16 gain_lever (1/16 code);
CRC-32. v2 blocks are not read (version check): after loading 0.2.45 the scope runs on the 1.00o5 compensations
until the base calibration is run again. GET_CAL 256 bytes: protocol section 14 row 51. Sector 708 bytes 432..487 hold
the provenance trailer (fw 0.2.52, row 55) and bytes 488..503 the FPGA 0x153A capture stages (fw 0.2.79, section 6.4).

**Bench procedure** (bench session and maintainer; back up SD sectors 700-710 first, the calibration writes 708 and 709):

1. FEL-load 0.2.45. GET_CAL ch 0 / 1: 256 bytes, byte 72 = 1, flags bit 0 clear (the v2 block on the card is not
   read). Diagnostics: "Calibration data: v1". Let the scope warm up 10 minutes.
2. Maintainer: probes and USB unplugged, System settings, Base calibration, OK: "Ch1.... OK", "Ch2.... OK" (about 7 s,
   2 s more than 0.2.43 for the full-rate slopes), "Calibration successful", then the yellow prompt "Gain: put both
   probes on the cal post, OK".
3. Maintainer: both probes switched to x1 (x10 works too: the firmware picks range 2 for x1 and range 5 for x10, step 1 of the
   gain step above; the bench used x1), both tips on the calibration post (ground clips not needed), OK: "Ch1.... OK",
   "Ch2.... OK", "Gain calibration successful". Reconnect USB.
4. GET_CAL both channels: byte 72 = 3, flags bit 3 set, bit 4 clear; block gain_range 2, gain_lever about 88 codes
   (1400-1420); CH1 gains (Q15 / 32768) gated A and B within 0.995..1.005, full A about 0.995, B about 1.028 (the bench
   predicts 0.9951 / 1.0278 / 0.9979 / 1.0021). CH2: record. Diagnostics: v3, the gains, "gain at range 2, lever ...".
5. Repeat the gain bench (DMM-read +-1.0045 V on CH1, range 2, DC, dcoffset 0, 4 captures averaged, READ_TRACE flags
   0, odd = A / even = B means; 200 MSa/s, 1 MSa/s, 10 kSa/s, CIC2 at 1 MSa/s). Pass: at 200 MSa/s |A - B| <= 0.15
   code at both polarities (was +1.25 / -0.58); at the gated rates A - B the same at both polarities within 0.15
   (what remains is core A's bow, about -0.5, not a gain); (mean(+1 V) - mean(-1 V)) / 2.009 V equal within 0.2 % at
   all four settings (was 28.17 full / CIC2 28.02 vs 28.48 gated); firmware volts ((code - zero_q8 / 256) x uv_per_lsb)
   within 0.5 % of the DMM at every rate (was 2.7-3.5 % low at full rate and CIC2).
6. Shorted inputs, both channels, the 0.2.43 zero checks: A - B within 0.1 gated, 0.15 at 200 MSa/s, means within 0.1
   of zero_q8 / 256, filtered = legacy.
7. Range assumption: generator static on the post (SET_GEN enable 1, freq_mode 1 phase_inc 0, duty_mode 1 threshold 0,
   then 0xFFFFFFFF), CH1 x1 at ranges 1, 2 and 3, 200 MSa/s and 1 MSa/s, 4 captures per level: corrected spans
   (high - low) of A and B agree within 0.3 % at every range and rate, and full vs gated within 0.3 %. If range 1 or
   3 misses while range 2 passes, the gains depend on the range: report it (v4 would store them per range).
8. Spectrum view with the 1 kHz square on the post, CH1: the fs/2 bin at 200 MSa/s at least 15 dB lower than on
   0.2.44; at 1 MSa/s legacy no worse (the gated gains barely change, the remaining spur there is INL).
9. Pass times (acquisition menu, GET_STATUS) at 20 us/div and 1 ms/div: within 1 ms of 0.2.43's 30 ms.
10. Skip and partial paths: run the base calibration again and touch outside the gain prompt: GET_CAL flags bit 4
    (kept) set, gains unchanged. Again with only the CH1 probe on the post: "Gain calibration failed", "CH2: gain:
    probe on post?", CH1 gains new (bit 4 clear), CH2 kept.

### 6.4 Calibration wizard (fw 0.2.59: four screens, plan section 6a)

Calibrate on the Diagnostics Status and Calibration pages and Calibrate now on the first-run screen open one modal flow
of four screens. Each is a step
dialog (640 x 300 at 80, 90) over the dimmed, frozen screen: title, one instruction, a result or progress line, Cancel
left (160 x 52) and the action right (180 x 52). Nothing is written before Save calibration; Cancel on any screen, also
while a measurement runs, writes nothing. Files: `cal_wiz.c/.h` (the steps as a pure-C state machine,
`host_tests/test_cal_wizard.c`), `cal_wiz_ui.c` (the dialog, the touch loop and the measurements), `ui_dlg.c` (drawing).

| Screen | Title | Instruction | Result | Action |
|---|---|---|---|---|
| 1 | Zero level | "Fully disconnect both probes and the USB cable from the device." / "Tap Calibrate." | per channel "CH1: measuring range 3 of 6" while it runs, then "CH1: zero level done" or the reason in words | Calibrate, Continue once both channels passed |
| 2 | CH1 gain | "Plug the CH1 probe back in and connect it to the calibration post." / "Set it to 1X on the handle. Tap Calibrate." | "CH1: checking the signal on the post", "CH1: measuring the gain, about 1 s", then "CH1 gain done", "No signal on CH1: tip on the post?" or the reason | Calibrate, Continue once passed |
| 3 | CH2 gain | "Plug the CH2 probe in and connect it to the calibration post." / the same | as screen 2 for CH2 | Calibrate, Continue once passed |
| 4 | Done | | per channel "CH1 calibrated: cores matched to 0.1 code, gain mismatch 1.3 %" | Save calibration |

While a measurement runs the action reads "Measuring..." greyed; Cancel stays live (checked between the measurement
points, about once a second; the measurement stops at the next one). A failure keeps Calibrate (retry); there is no Skip.
Screen 1 shows "USB is connected: unplug it before Calibrate" in amber while a host is attached (the MUSB frame number
moves; checked twice a second; Calibrate is not refused). Both channels are always measured in the zero step; nothing
is used unless both pass. On a bitstream without the generator (stock 0x1432) screen 2 says "Gain calibration needs the
open FPGA bitstream." / "Continue keeps the gains and saves the zero level." and Continue leads to Done. "Cores matched"
is the zero step's worst |drift A - drift B| over ranges and classes (one decimal, at least 0.1), "gain mismatch" the
full-class |G_A / G_B - 1| the correction now removes; without a gain step the line says "earlier gains kept" (carried
with flag KEPT, GET_CAL flags bit 4) or "gains not measured".

Save calibration writes the PWM centres (`dc_calibration_offset[0..6]`), the 1.00o5 compensations, the block, sectors
708 and 709 (as the 0.2.45 base calibration did), then `cal_wizard_committed()` (the v4 tracker reset); the dialog
closes and the screen under it comes back. With the volts factors missing (`calibrationfail`, sector 708 did not load)
the Diagnostics and first-run callers go on to the volts calibration (section 5 "Screens: Diagnostics"). Each measurement batch restores the channels, sample rate, trigger mode
and, after a gain step, the generator.

The level check (both post levels at range 2, gated, 2 conversions each) and the measurements are fw 0.2.50's
(`cw_level_ok`: a x1 probe gives 88 codes at range 2, a x10 probe 88 at range 5; README 6.3); the silent CH2 check after
CH1 and the Skip button are gone with the per-channel screens.

**Bench** (maintainer; back up SD sectors 700-710 first, Save writes 708 and 709):

1. Menu > Diagnostics > Calibrate with USB connected: screen 1 with the amber USB line; unplug USB: it goes within a
   second. Cancel: the Diagnostics page comes back, GET_CAL unchanged (after USB is plugged in again).
2. Probes and USB off, Calibrate: "CH1: measuring range 1 of 6" .. "CH2: measuring range 6 of 6" (about 8 s; the button
   reads "Measuring..."), then two ticks "CH1: zero level done", "CH2: zero level done", Continue.
3. Screen 2 with no probe on the post: Calibrate gives "No signal on CH1: tip on the post?", Calibrate stays. CH1 x1 on
   the post: Calibrate, "CH1 gain done", Continue. Screen 3 the same for CH2.
4. Done: two lines in the channel colours; Save calibration: the dialog closes; the Status page shows the calibration
   OK (or Check with the reason) and the provenance of this power-on; GET_CAL flags bit 3 set, bit 4 clear.
5. New run, Cancel during the zero step's measurement: it stops within about a second and nothing changed. Stray
   touches off the two buttons do nothing on every screen.

**Failure note and CAL_PROBE (fw 0.2.67, protocol 1.21 provisional, row 61).** A channel that fails the Zero level
step gets a second, small line under its result (13 px font; the two results move up to make room): "Range 1, 200
MSa/s, zero point, PWM 812: A 130.1 (126..135), B 125.9 (101..152)", or for a clip "..., B 150.0 (0..255, 12 clip)".
It names the measurement point the failure was found at (`acal_fail_point`: a clip at the first point after the
coarse ones with a sample at 0 or 255, noise and drift at the zero point of the failing class, a missing response at
the coarse high point), its PWM, and per core the raw mean, the lowest and highest code and the samples at 0 or 255.
The zero step of one range is `cal_range` (cal_wiz_ui.c), shared by the wizard and CAL_PROBE, which records all eight
points (`acal_point`, ACAL_PT_*: coarse 500 / 1200, gated centre -150 / +150 / centre, 200 MSa/s centre / +150 /
-150).

**Zero at the centre (fw 0.2.79).** The zero of each class is measured at the refined centre PWM, where the level sits
at code 128 (8 conversions gated, 64 at 200 MSa/s). 0.2.67-0.2.78 measured it as the mean of two points 12 codes either
side (`acal_zero_split`, CAL_PROBE byte 248) so the step passed on FPGA 0x1538 / 0x1539, whose 200 MSa/s capture garbled
cores near 0x7F / 0x80; the maintainer removed that workaround (2026-10-19 20:30, fail fast), so CAL_PROBE byte 248 is always
0. A clip found only at 200 MSa/s while the gated point at the same PWM is quiet and within 8 codes of 128 reports "ADC
readout fault near code 128: check the FPGA bitstream" (ACAL_E_READOUT, `acal_readout_fault`): such a bitstream fails
the step instead of being calibrated around the fault.

**Capture stage training (fw 0.2.79, FPGA 0x153A, protocol 1.24, row 64; `cap_stage.c`, `cap_hw.c`).** On 0x153A each
channel's zero step starts with the training of its two cores' capture stages (`cal_train`): range 0, the input parked at
code 128 by the gated centre search, a quiet input required (gated within-capture RMS at most 1 code since fw 0.2.100,
3 codes before, else "too much noise"). Each core is tried at stages 2..5 (0x63), one 8000-word 200 MSa/s capture each with the 0x36 outlier counts read
(threshold 0x64 = 8 codes; a clean stage reads 0). Per core the clean stage of {3, 4} whose outer neighbour is clean too
is taken (the power-on one when the clean run is symmetric), else the clean one of {3, 4}, else a clean reserve 2 / 5; a
B core with no clean stage is tried again with its reference bit (0x63 bit 12 / 13, timed by the chip's A encode). A core
with no clean stage fails the channel with "ADC readout fault near code 128: check the FPGA bitstream"; a passed
channel's note reads "Capture stages: CH1 A4 B4". Save stores the value with the calibration in sector 708 bytes
488..503 (u32 'ACAP', u16 version 1, u16 length 16, u16 the 0x63 value, u16 the FPGA version word, u32 CRC-32 of the 12
bytes before; after the provenance trailer, outside the v3 block, so an older firmware's 708 save clears it); Cancel and
CAL_PROBE put the previous 0x63 back. At boot (and after a calibration load) 0x64 = 8 and 0x63 = the stored value when it
was trained on the running bitstream, else the power-on 0x08E4. After every accepted full-rate capture the firmware reads
0x36 and flags a core whose count exceeds its chip partner's by more than 8 (GET_HEALTH 104..123, event 23). Bench list:
firmware/AGENTS.md section 54.

**Band training (fw 0.2.81).** Bench 2026-10-20 on 0x153A (threshold 32 codes since 0.2.80): a bad stage can garble only
at some codes and in some captures (CH1 A4 on the maintainer's unit: rail samples and jumps only while the samples sit at codes
127..135), so one capture at the centre read it clean and the wizard kept A4. The training now parks the quiet input at
six codes, 120, 124, .. 140 (`cs_band_pwm`: the offset PWM interpolated from the centre search's two gated slope points),
and at each point takes two 8000-word captures per stage 2..5; the 0x36 counts are summed per core and stage over the
band (threshold 32 codes) and a stage is clean when its sum is 0. `cs_pick` then takes, per core, the clean stage inside
the widest run of clean stages with the most clean stages on both sides, 3 or 4 before a reserve 2 / 5, the power-on
stage only as the tie-break inside that run (CH1 A clean at 2, 3, 5 gives 3; clean at 3, 4, 5 gives 4; clean at 2..5
keeps the power-on stage). The B reference-bit retry and the fail-fast "ADC readout fault near code 128" are as before.
The wizard shows "CH1: training capture stages..." while it runs: 6 PWM points of 60 ms settling plus 48 captures,
about 0.4 s per pass (twice with the reference pass), so a channel's zero step grows by about half a second.
In operation a core the self-check flags in 3 captures within a minute (`cs_watch`) sets GET_HEALTH 106 bit 5
(REPEAT): the Diagnostics Status verdict turns amber with "ADC capture flagged 3 times within a minute: run Calibrate.",
the Calibration page line adds "calibrate", and event 23 is logged once per core (no longer whenever the flagged set
changes). It clears when a training is saved; nothing retrains outside the wizard.
CAL_PROBE runs it from the host with the UI stopped (mode 0, about 1 s), or runs the whole zero step exactly as a
Calibrate tap does, over the dimmed screen with the dialog, progress and Cancel polls (mode 1, about 15 s);
`python firmware/tools/cal_probe.py <port> both all` prints every point (arguments: port, CH1 / CH2 / both, ranges, repeats,
settle, prestrobe, mode). Nothing is stored.

**Full-rate capture check (fw 0.2.61, per capture since fw 0.2.62, `enc_heal.h`).** Every 200 MSa/s conversion of the
zero and gain steps is checked before the wizard reads it, like every other full-rate capture (section 5 "Full-rate
capture check"): on FPGA 0x1538 by the FPGA's own encode check (read 0x33), on 0x1532..0x1537 by the RMS of core B
against its core A neighbours. A garbled capture is discarded and the conversion re-armed, up to 16 times. If one
stays garbled, the range fails with "ADC capture timing: the 200 MSa/s data stays garbled" (ACAL_E_TIMING). The
wizard's zero step reads raw codes per core, so it passes on 0x1537 and 0x1538 alike; the stored per-core zeros and
gains of a calibration taken on 0x1537 are on the wrong cores of chip 1 once 0x1538 is flashed (Diagnostics says so on
0x1537): calibrate again after flashing 0x1538. Calibration from 0x1535 fits 0x1538.

### 6.5 Calibration v4: background interleave tracking (fw 0.2.50, protocol 1.15)

Design and numbers: docs/plans/interleave-tracking.md; wire: protocol row 54 (1.15; the branch wrote it as row 53 / 1.14). Files: `acal_track.c/.h` (pure C: the
A - B estimator and fit, the class-probe check and the apply functions; `host_tests/test_acal_track.c`), `acal_live.c/.h`
(the per-channel state, the readout hooks, the class-probe scheduler).

**Why.** The gated class (every legacy time base but 200 MSa/s) keeps the one-time v3 calibration for hours. The
full-rate class (200 MSa/s and every filtered capture: both cores at 100 MHz, interleaved) does not: on the bench the
core A/B gain ratio read 1.033, 1.013 and 1.000 within a night, the A/B zero split moved 1.1 code within 15 minutes, a
bow of about 1 code at 35-50 codes from mid-scale came and went, and both cores rose 0.7-0.8 code in two hours against
the gated class. Symptoms: a 1-2 code sample-by-sample split and an fs/2 spur at 200 MSa/s, and a filtered zero 0.7 code
off.

**A - B tracker.** Every 8-bit full-rate readout of CH1 / CH2 with a bound v3 block (UI records and host captures, at
most one per 50 ms per channel) is offered to the channel's tracker after the readout. Each interior sample is compared
with the mean of its two neighbours from the other core where all three are within 4..251 and the neighbours differ by
at most 6 codes (flat parts: plateaus, slow slopes, a quiet input); a record counts with at least 256 such pairs and at
most 1/8 dropped for slope (edges and energy near fs/2 are rejected: a 40 MHz sine is never accepted). The sums go into
moments with forgetting 1/256 per record (tau 256 records, 12.8 s at the 50 ms gate) and a least-squares fit gives
A - B = d + e x + q x^2 (x = code - 128). d is always updated, e only with at least 8 codes RMS of signal spread, q only
when the signal visited three separated levels; otherwise the last value is held. d and e are applied in the full class
only, half to each core with opposite signs (folded into the readout's per-core gain and offset: no cost per sample);
the mean of the cores, and with it the filtered path, the volts and the trigger level, is unchanged. q is estimated and
shown, not applied.

Host-test numbers (`test_acal_track`, a drifting two-core model with the v3 correction, dither, clamp and rail rule):
a 1.7-code step of core A falls under 0.1 code (split and fs/2 amplitude) from 736 records on (36.8 s, 2.9 tau); a 3 %
gain ramp leaves under 0.1 code at +-40 codes 1 tau after the ramp ends; noise floor of d 0.0012 code (quiet input) to
0.004 code (sine), of e 4e-5; a quiet input holds e. Bow: the fit finds q within 2 % of the model and e unbiased by it;
the residual it leaves is q x^2, for a bow of 1 code at 42 codes 0.66 code at -35 and 1.6 codes at +53 (the post
square's levels), 0 at mid-scale.

**Class probe.** The tracker cannot see the common mode of the full class (it keeps the mean of the cores). The only
stable reference is the gated class, so a probe compares the two on the live input: with the channel's relays, offset
PWM and coupling as they are, 8 conversions at 2 MSa/s, 64 at 200 MSa/s (they spread over the 25 kHz offset-PWM ripple),
8 at 2 MSa/s again, about 100 ms, through a private raw scratch channel (the screen's traces stay). c = corrected mean
of both cores at full rate minus the mean of the two gated parts; valid when every part's RMS per core is at most 2
codes, nothing clips, the gated parts agree within 0.3 code and |c| <= 8 codes. The first valid probe after a reset sets
cm = c, later ones move it by a quarter; cm is subtracted from both cores in the full class (8-bit) and from corr16 (the
16-bit path: every filtered capture, the spectrum view, the 16-bit trigger level; it is added after the conversion, so it
does not depend on N, K or the FIR gain). It runs per channel at most every 30 s (the first 5 s after boot) and only
when nothing else is going on: RUN, scope or spectrum view, no menu (it runs from the main loop), no UI conversion in
flight, no host capture armed or triggered, no READ_RAW hold, the channel on and its last record quiet (RMS <= 2 codes).
A periodic signal never qualifies; cm then holds (its age grows). Cost: one missed screen update, and the FPGA ring no
longer holds the last UI capture (as after any calibration conversion). `EXTRA_CFLAGS=-DPC_CAL_CLASS_PROBE=0` builds it
out (cm then stays 0).

**Not tracked.** The gated class (it is the reference; a step like the 03:48 one, -1.7 code on CH2 core A in both
classes, stays in the gated class until the next calibration: the full class follows it relative to the gated class),
16-bit readouts as tracker input (they weigh both cores equally, the split cancels), the calibration's own conversions.
Nothing is persisted: tracker and cm restart from the stored v3 values at boot and after the wizard's Done.

**The one-time gain step stays.** The tracker sees only A - B of full-rate records. The full class against the gated
class (1.1 % on the bench, what every filtered capture reads with) and the gated gains come only from the step, and it
gives the tracker its starting point.

**Readout.** GET_CAL 288 bytes (row 54): 256 flags, d, e, q, ages, counts, cm; bytes 84 and 92..107 include v4.
Diagnostics Calibration page (fw 0.2.59), per channel: "Tracking A-B +0.12 code, slope +0.31 %, 3 s ago" (applied
A - B offset and slope, age of the last accepted record) and "Full-rate offset +0.70 code, 4 probes, 12 s ago" (common
mode subtracted, valid probes, age); grey until they ran. The bow q is in GET_CAL and the saved report.

**Bench** (maintainer with the bench session on <port>; FEL load only, nothing written to the card):

1. Wizard: the walk-through of section 6.4 (steps 1-5). After Save calibration, GET_CAL bytes 256..287 are reset
   (flags 0, ages 0xFFFFFFFF) and the Calibration page's tracking lines are grey.
2. Shorted inputs (BNC caps or probe tip to ground clip), DC, range 2, dcoffset 0, 200 MSa/s, 30 minutes, then CIC2+FIR
   at 1 MSa/s for 10 minutes. Every minute: GET_CAL CH1 and CH2 (flags, d, e, q, age, records, cm, cm_probes, cm_age)
   and 4 x READ_TRACE. Expect: track flags bit 0 within a second, bit 1 clear (quiet: e held), cm_probes counting about
   twice a minute, cm moving to the full-class drift (0.7-0.8 code after two hours on the bench, smaller fresh after a
   calibration); corrected A - B (mean of odd minus mean of even trace positions) within 0.1 code throughout; after the
   switch to CIC2+FIR the trace mean within 0.15 code of 128.
3. +1 V and -1 V (DMM-measured) on CH1 range 2, 200 MSa/s, 30 minutes each. Expect A - B within 0.1 code at both
   polarities after 1 minute, the fs/2 spur in the spectrum view at the floor (record the level), cm unchanged by the
   level (the probe is input-independent for DC). Record e and q (q x 1600 = the bow at 40 codes in the diagnostics
   line) at both polarities and once with the calibration-post square (generator on, 1 kHz): q there tells whether the
   bow correction (design section 6) is worth building.
4. Negative checks: a 40 MHz signal or the post square at 200 MSa/s with fast edges must not move d (records rejected or
   few pairs; `last_pairs`), a running sine must not trigger class probes (cm_age grows), opening a menu or arming a
   host single stops probes, and a probe never shows as a glitch in the trace (the screen just skips one update).
