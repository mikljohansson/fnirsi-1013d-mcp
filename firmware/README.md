# Firmware

```
firmware/scope/       scope application, Atlan4 v1.00o5 source + plain Makefile
firmware/tools/       mksunxi.py, make_sd_image.py, fetch_gcc10.sh
firmware/reference/   Atlan's prebuilt v1.00o5 binaries (not committed, see its README)
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
wsl -d Ubuntu -- bash -lc "cd /mnt/d/src/fnirsi-1013d-ai/firmware/scope && make -j"
```

Outputs in `firmware/scope/build/`:

| file | what | upstream name |
|---|---|---|
| `fnirsi_1013d.elf` | linked ELF (with `-g`) | `fnirsi_1013d_scope.elf` |
| `fnirsi_1013d.bin` | eGON.EXE image, mksunxi-fixed. Goes to SD sector 750 or is loaded over FEL | `fnirsi_1013d_scope.bin` (the updater's `scope.bin`) |
| `fnirsi_1013d.map` | link map | - |
| `fnirsi_1013d_sdpackage.bin` | `make package`: SD package (bootloader+updater+scope from sector 16) | `fnirsi_1013d.bin` |

Targets: `make`, `make clean`, `make V=1`, `make package`, `make fel`, `make info`
(`objdump -h`), `make check-reference`, `make gcc10`. Parallel builds and `-MMD -MP` dependency
tracking work. `make` builds the USB control protocol in by default; section 5 has its options.
`SOURCE_DATE_EPOCH=<unix time>` pins `build_time.h` to that time in UTC. Without it the
file uses the current local time, like upstream. `CROSS_COMPILE=` selects another toolchain and
`EXTRA_CFLAGS=` adds flags.

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
for that. `make package` replaces `flashfilepacker -l 0x5BC00` and needs a base image.

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

- **Byte-identical with Atlan's toolchain.** `make check-reference` builds with `PC_PROTOCOL=0`
  (section 5), so it also proves that the protocol hooks leave the base untouched when disabled.
  It builds with GCC 10.3.1 from
  `tools/fetch_gcc10.sh`. That script downloads the jammy `.deb`s over https, checks each against a
  pinned sha256, and unpacks them into `~/opt/gcc10` without installing them: binutils 2.38 and newlib
  3.3 headers. The build uses Atlan's `build_time.h`
  (2026-02-13 18:59:33), and the result `cmp`s **identical** to
  `reference/fnirsi_1013d_v1.00o5_atlan.bin`. `make package` with that build is also identical
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
For a first hardware test, the GCC 10 build is the conservative choice:
`make check-reference`, then use `build-gcc10/fnirsi_1013d.bin`.

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
| 0 | MBR. The FAT32 partition must start at sector 2048 or later. | | |
| 16 | bootloader `eGON.BT0` (Atlan "BOOT fw v0.8" in the v1.00o5 package) | 32 KiB, sectors 16-79 | BROM loads it to SRAM 0x0 |
| 80 | updater `eGON.EXE` (v0.02v in the package) | up to sector 707 | boot mode 3 |
| 708 | calibration data | 1 sector | scope / updater |
| 709 | scope settings | 1 sector | scope |
| 710 | display + touch config, boot-mode byte | 1 sector | loader tool / scope |
| 750 | scope `eGON.EXE` (`build/fnirsi_1013d.bin`) | must end before the partition; 681 sectors now, ends at 1430 | boot mode 0 |

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

**Warning about the upstream `dd` instructions.** `dd if=fnirsi_1013d.bin of=/dev/sdX bs=1024
seek=8` writes the whole package from sector 16 through 1425. Atlan's package has zeros at
708-710, so this command wipes calibration, settings and the display/boot-mode sector. Use
`make_sd_image.py write`, which writes only the parts you pass. A GPT card cannot be used,
because the GPT entries occupy sectors 2-33.

### make_sd_image.py

```
python3 tools/make_sd_image.py info  card.img                 # or an SD package / card dump
python3 tools/make_sd_image.py extract reference/fnirsi_1013d_v1.00o5_atlan_sdpackage.bin -d parts/
python3 tools/make_sd_image.py write --image card.img --scope scope/build/fnirsi_1013d.bin
python3 tools/make_sd_image.py write --image card.img --bootloader parts/bootloader.bin \
        --updater parts/updater.bin --scope scope/build/fnirsi_1013d.bin --boot-mode 4
python3 tools/make_sd_image.py pack -o pkg.bin -i scope.bin -l 0x5BC00   # = flashfilepacker
```

`write` behaves as follows:

- It checks each part's magic, mksunxi checksum and region size, and that no part overlaps an
  MBR partition.
- It refuses GPT cards, and refuses device-looking paths passed to `--image`.
- It sets only byte 31 of sector 710 for `--boot-mode`.
- It reads every write back to verify it.
- `--create` makes a new zero-filled image, 1 MiB by default.
- A physical card needs `--device /dev/sdX`, or `\\.\PhysicalDriveN` on Windows (untested). The
  card must have an MBR. You must type the device path to confirm. Sectors 0 to the last written
  sector are always backed up to `backups/` before writing.

Take and verify a full card backup before any of this, as CLAUDE.md requires. During
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
mcp/.venv/Scripts/fnirsi1013d-flash.exe --image firmware/scope/build/fnirsi_1013d.bin --port COM10
mcp/.venv/Scripts/fnirsi1013d-flash.exe --no-fel                          # already in FEL (boot menu)
```

It runs `#VER` and GET_INFO, sends ENTER_FEL (without the capability it asks for the boot menu instead), polls
`usbipd list` for `1f3a:efe8`, attaches it with `usbipd attach --wsl --busid <id>`, runs
`wsl -d Ubuntu -u root -- sunxi-fel version` and the `-p write 0x7FFFFFE0 <image> exe 0x80000000` load (the
image path converted to `/mnt/d/...`), then waits for 1209:0001 and prints its `#VER`. Each step is printed with
its time; the exit code is non-zero on failure (2 image, 3 scope port, 4 FEL device / usbipd, 5 sunxi-fel,
6 the port did not come back). The FEL device must have been shared once from an admin shell
(`usbipd bind --hardware-id 1f3a:efe8`). On a Linux host the same command calls sunxi-fel directly
(`--sunxi-fel "sudo sunxi-fel"` if the udev rule is missing). Options: `--distro`, `--fel-timeout`,
`--port-timeout`. Only RAM is written; announce the reload at the bench first (CLAUDE.md).

### USB from Windows

- **WSL with usbipd-win** (5.3.0 installed on this machine). Run from an admin PowerShell while
  the scope is in FEL:

  ```
  usbipd list                                   # find 1f3a:efe8 and its BUSID
  usbipd bind --busid <BUSID>                   # once
  usbipd attach --wsl --busid <BUSID> --auto-attach
  wsl -d Ubuntu -u root -- sunxi-fel version
  wsl -d Ubuntu -u root -- bash -lc "cd /mnt/d/src/fnirsi-1013d-ai/firmware/scope && make fel"
  ```

  Run as root, or use `make fel SUNXI_FEL="sudo sunxi-fel"`, because WSL normally does not apply
  the sunxi-tools udev rule. The device re-enumerates after `exe` or a power cycle, and
  `--auto-attach` re-attaches it.
- **Native Windows:** install the WinUSB driver for `1f3a:efe8` with Zadig, then use a Windows
  build of sunxi-fel or xfel. Do not mix the two approaches on the same device, because a bound
  usbipd device is not visible to Windows tools.

On the owner's unit, FEL entry from the boot menu, `sunxi-fel version` through usbipd, and a FEL
RAM load of the v1.00o5 scope image (`sunxi-fel -p write 0x7FFFFFE0 scope.bin exe 0x80000000`)
were verified on 2026-10-18 (`docs/hardware-status.md`). Hardware steps are done by the owner.

## 5. PC control protocol (USB CDC)

The scope speaks the binary protocol of `docs/protocol.md` on its USB port. The integration plan is
`docs/plans/firmware-usb-protocol.md`. Deviations from the spec are listed in the "Implementation
notes" section of `docs/protocol.md`.

### Build options

| make variable | default | effect |
|---|---|---|
| `PC_PROTOCOL` | 1 | 0 builds Atlan's v1.00o5 unchanged: every hook compiles to nothing |
| `PC_DEBUG` | 0 | 1 adds DEBUG_FPGA and FORCE, sets caps DEBUG_RAW and FORCE_TRIGGER. Bench builds only |
| `PC_FORCE_CDC` | 1 | CDC at boot even if the saved "USB CH340 mode" setting says MSC |
| `PC_FIFO_MAP` | 0 | USB endpoint FIFO layout (`pc_transport.h`). 1 = the fw 0.1.3-0.1.5 layout, fw version gets `-m1` |

```
make                          # GCC 13, protocol on: build/fnirsi_1013d.bin
make gcc10                    # same with Atlan's GCC 10.3.1: build-gcc10-pc/fnirsi_1013d.bin
make PC_DEBUG=1 BUILD=build-dbg
make check-reference          # PC_PROTOCOL=0 with GCC 10.3.1, must print IDENTICAL
make -C host_tests test       # host gcc: frame layer, vectors, session tests (scope/host_tests)
```

`make check-reference` always builds `PC_PROTOCOL=0` and compares with Atlan's binary. A GCC 13
`PC_PROTOCOL=0` build is also identical to a GCC 13 build of the tree before the protocol. Neither
compiler gives a new warning with the protocol on, in release or `PC_DEBUG` builds.

Files:

| file | role | host build |
|---|---|---|
| `scope/pc_protocol.c/.h` | frame parser and encoder, CRCs, ASCII probe lines | yes |
| `scope/pc_cmds.c/.h` | dispatcher, handlers, shared control with the UI (protocol 1.3), host single capture | yes, via `pc_hal.h` |
| `scope/pc_hal.h` | the only interface `pc_cmds.c` uses to reach the scope | |
| `scope/pc_hal_scope.c` | `pc_hal.h` on the 1.00o5 settings and `fpga_control.c`, firmware glue | no |
| `scope/pc_transport.c/.h` | USB RX ring, TX pump, ZLP, link events | no |
| `scope/pc_rx_ring.h` | RX ring copies and the one-FIFO-read-per-packet store | yes (`test_rx_ring`) |
| `scope/pc_hooks.h` | the hooks in the 1.00o5 files, empty with `PC_PROTOCOL=0` | |
| `scope/host_tests/` | `test_protocol`, `test_rx_ring`, `test_session` (mock FPGA and UI), `check_vectors.py` | |
| `../docs/protocol-vectors.json` | shared frame vectors (`gen_vectors.py`; the MCP server merges its own) | |

Hooks in the 1.00o5 code, all under `#if PC_PROTOCOL`:

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

### Hidden menu options (fw 0.2.6)

Owner policy: the UI offers only what the running hardware can do. Protocol builds hide these 1.00o5 menu items.
Since 0.2.8 the generator and trigger menus are compacted (owner: no gaps): the kept rows move up and the menu
boxes shrink (generator 3 rows, 169 px: Power, Freq, Duty; trigger 5 rows, 280 px: Always 50 % moves into the hold-off
slot), via `PC_GEN_DY` / `PC_TRIG_DY` in `pc_hooks.h` and the `*_MENU_HEIGHT` values in `variables.h`, which also
size the screen save/restore and the touch area. Items inside a row (memory boxes, EXT, the system-settings
Time & Date row) are still painted over (touches ignored; `pc_glue_ui_has` / `pc_glue_ui_mask` in `pc_hal_scope.c`,
hooks `PC_UI_HAS` / `PC_UI_MASK` that are empty with `PC_PROTOCOL=0`):

| item | where | hidden when | why |
|---|---|---|---|
| Wave SQUR / SINE / TRGL | generator menu | always | no DAC: the generator is the FPGA's DDS square wave on the calibration pin; the setting was never sent anywhere |
| Ampl, Offset, Phase | generator menu | always | no DAC; the FPGA writes are commented out in 1.00o5 |
| the generator menu itself (top-bar button does not open it) | top bar | stock bitstream 0x1432 (no DDS) | the stock core has a fixed 1 kHz square wave |
| memory depth boxes above the bitstream's maximum | acquisition menu | 6000+ on stock (3000), 15000 on EF2/EP4 (12286) | the FPGA cannot capture more |
| Long memory | acquisition menu | always | 1.00o5 forces it off; the main-loop branch is empty |
| Filter selector (in the Long memory slot) | acquisition menu | bitstream without DECIM (all but AL3 0x1534+) | the decimation filter exists only in fpga/al3 0x1534 (fw 0.2.30, "Decimation filter" below) |
| memory depth 9000, 12000, 15000 | acquisition menu | Filter on (0x1534+) | the 16-bit layout holds one sample per ring word: at most 8190 samples |
| EXT trigger source | trigger menu | bitstream without EXT_TRIGGER (all but AL3 0x1533+) | the input exists only in fpga/al3 0x1533 (calibration pin, 3.3 V logic referenced to the scope's USB ground, not the probe ground, which sits ~1.24 V above it); selecting it switches the generator off |
| Hold off | trigger menu | always | never sent to the FPGA in 1.00o5 |
| Spectrum analyzer row | main menu | roll mode, viewers (fw 0.2.28) | the roll-mode trace path does not run the spectrum hook; the menu is one row shorter then |
| FFT switch ("Open FFT" row) | channel menus | always (fw 0.2.26) | 1.00o5 stores `fftenable` and draws nothing; Invert, Coupling and V/A move up one row (`PC_CH_DY`), the empty slot sits above Probe mode (the menu box keeps its height for the V/div column) |
| Time & Date settings (RTC submenu) | system settings | no DS3231 answers at boot | the clock, file stamps and FAT times would read garbage |
| Temperature (diagnostics screen) | diagnostics | no DS3231 answers at boot | it is the DS3231's temperature register; without the module it read "99" |

Relabelled (fw 0.2.10): the acquisition menu's "ACQ / Long mode" box (it named the mode a tap switches to) is drawn
as a state toggle, "Roll mode" + "on" / "off", green when the long (roll) time base is on, like Average and Long
memory. It stays: roll mode works on this hardware. System settings (fw 0.2.13): without a DS3231 the Time & Date
row (the last one) is not drawn and the menu box is one row (59 px) shorter; `SYSTEM_MENU_HEIGHT` is a runtime value
in protocol builds (`pc_glue_sys_menu_height`), feeding the box, separators, slide-in and screen save/restore.

Battery indicator (fw 0.2.13): the level is a running average, and while charging it changes only when the average
moves 1.25 steps away from the shown value (the raw level followed ADC noise while charging over USB and the last
column flapped). Off the charger the 1.00o5 rule stays (the level only drops), applied to the average.

Version display (fw 0.2.7): the main-screen corner and the restore screen show `v<fw>`, the diagnostics screen shows
`fw <fw>` in place of 1.00o5 and a full identity line along its top edge (`FNIRSI-1013D-AI fw .., proto 1.3, base
Atlan4 1.00o5, git <hash>, built <date>`), all from PC_FW_VERSION / PC_PROTO_* / PC_FW_BUILD (the same as `#VER` and
GET_INFO). Waveform and reference files still store 1.00o5's VERSION_STRING (file format data).

At boot a saved setting that selects a hidden option is corrected: RTC off without a module, EXT trigger -> CH1,
memory depth clamped to the largest offered step. The DS3231 is detected by addressing it (0xD0) on the touch
panel's I2C bus and sampling the ACK bit [U, bench check: with and without a module]. No option needs a manual switch
yet (everything above is detected); new hardware mods that cannot be detected get a switch in "Other settings"
(the menu is full: a second column or page is needed) with the value in a spare settings word (108..119 or 204..255,
`scope_save_config_data`; 207 holds the Filter setting since 0.2.30), default off. Not changed: trigger positions 25 % / 75 % on the stock bitstream (software
only there), the generator top-bar button text on stock.

### Decimation filter (FPGA 0x1534, fw 0.2.30, protocol 1.9)

fpga-gen3's AL3 bitstream 0x1534 filters the 200 MSa/s ADC stream before decimating (fpga/al3/src/FPGA commands.txt,
fpga/al3/README.md): 0x53 mode (K: 0 legacy sample skipping, 1 boxcar, 2 CIC2, 3 CIC3; bit 2 core-order swap;
bits 3 / 4 capture the A / B cores on the falling write-clock edge), 0x54 output shift S, 0x55 16-bit trigger level,
read 0x29. Latency 120 ns pin to memory for every K and N, the sample centred K (N - 1) 2.5 ns earlier still. Decimation N = 2 x (floor(D / 2) + 1) for the 0x0D
divider D, so the odd dividers of `sample_rate_settings` give the same rates as before; the FPGA stores
min(65535, y >> S), y = filter output with gain N^K. On 0x1532/0x1533 none of this is written (caps bit 15 DECIM).

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
  slot of the hidden Long memory box, only on 0x1534+. Saved in settings word 207 (0xDE00 | K); older sectors read
  as off. Turning it on with 9000+ samples sets the memory depth to 6000 (trigger point kept at 25 / 50 / 75 %).
  The box shows the setting; GET_DECIM tells the effective K (e.g. legacy at 200 MSa/s).
- Protocol 1.9 (docs/protocol.md section 14 row 47): GET_DECIM 0x020C, SET_DECIM 0x020D (shared control: it is the
  UI setting; SET_TRIGGER total > 8190 is refused while it is on), READ_TRACE flag bit 2 (u16 samples),
  READ_RAW layout 2 (u16 little-endian words), GET_CAPTURE_INFO 76 bytes with the capture's mode, S and N.
- Host tests: `test_session.c` `test_decim_math` (all table rates x K, exactness limits against 128-bit references,
  L16 round trip for every level, compensation, rates, settle) and `test_decim_protocol` (0x1533 untouched, 0x1534
  flow); `decim_check.c` reproduces the 80 "decim" cases that `gen_vectors.py` computes in Python.

Bench (nothing verified on hardware; bitstream not yet flashed): 0x29 read-back after an arm (GET_DECIM bytes
26-27); a DC level reads the same code with the filter off and on; a 3.9 MHz generator at 1 MSa/s aliases at
100 kHz with skipping and is attenuated with boxcar / CIC (plan F1); trigger on CH1 with the 16-bit level at
several rates; the swap bit (T4 dependency); settling after a time base change (first capture clean).

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

**Speed (fw 0.2.31).** The MMU and so the D-cache are off, so every data access, the stack included, goes to DDR:
about 90 CPU cycles each on the bench (0.2.27 FFT_BENCH: 4096 points in 24.3 ms, 4x the estimate; the C loops spilled
to the stack and GCC 10 never paired accesses). Since 0.2.31 the hot loops are ARM assembly inside `fx_fft.c`,
everything in registers: the u8 load with window and bit reversal fused with stage 1 (two LDRH, two LDRD, one 4-word
STM per point pair; the window drops to Q22 there so the product fits 32 bits, error below 2^-22), the stages (per
twiddle where blocks outnumber twiddles: 2 LDRD + 2 STRD per butterfly; per block otherwise: + 1 LDRD twiddle), and
the split step (2 + 1 LDRD, 2 STRD). Each has a C twin computing the same integers, which the host build runs.
Measured on 0.2.31: 4096 points 10.9 ms (was 24.3), 2048 points 4.9 ms in DDR, 2.3 ms in SRAM. Since 0.2.37 the dB
conversion between DC and Nyquist is assembly too: one LDRD per bin, the log2 table as (t, delta) pairs (one LDRD),
the window norm, shift and dBV offset folded into one 64-bit constant, so 3 accesses per bin instead of about 9
(byte-identical to the C twin, including the clamps; DC and Nyquist stay in C). Enabling the MMU with cached DDR
would beat all of this.

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

### Spectrum view (`spectrum.c`, fw 0.2.28)

A display mode of the main loop, not a modal menu, so the MCP's polling and the shared screen keep working (plan:
`docs/plans/dpos350p-features.md` 2b). Main menu, sixth row "Spectrum analyzer" (hidden in roll mode, where the
trace path is different); in the view the row reads "Scope view" and a tap returns. The strip's "Scope" field does
the same. While on, `scope_display_trace_data` (hook `PC_SPECTRUM_DISPLAY`) draws the trace area itself and touches
in it go to the strip (`PC_SPECTRUM_TOUCH`); the top bar, the MCP chip and the right column (RUN/STOP, AUTO, menus)
are 1.00o5's. Each new capture (UI or host) is transformed once; settings changes recompute from the held buffer,
so a stopped capture can be re-examined. Roll mode or a viewer ends the view.

Screen: info line (sample rate, N, RBW; peak frequency and level per channel in its colour), plot of 10 x 8
divisions with dBV labels (dBA in A mode), start / centre / stop frequency labels, and the strip. Each plot column
shows the highest bin it covers, so narrow lines survive at 720 columns; zoomed in past one bin per column it draws
straight lines between bins. A white triangle marks the peak (DC bins skipped). Below 200 MS/s an orange line says
"no anti-alias filter": the FPGA decimates by skipping samples (until a filtering bitstream lands).

| field | values | tap |
|---|---|---|
| Channel | CH1, CH2, CH1 + CH2 (only enabled channels) | next |
| FFT length | 1024 .. the largest power of two the record holds, at most 4096 (2048 at depth 3000) | next, wraps |
| Window | Hann, flat top, rect | next |
| Scale | 5, 10, 20 dB/div | next |
| Ref level | -100 .. +60 dBV at the top line, one division per step | left half down, right half up |
| Auto | sets FFT length, window, scale, ref level and span from the signal (rules below) | once |
| Reset | back to the defaults: 2048 points, Hann, 10 dB/div, ref 0 dBV, full span, Spectrum | once |
| View | Spectrum, Waterfall, Density (fw 0.2.32) | next |
| Scope | | leave the view |

With the 0x1534 decimation filter on (fw 0.2.34) the view transforms the filter's 16-bit record (`pc_hal_trace16`)
instead of the 8-bit trace, with the filter's rate 200 MHz / N (N = 2 (floor(D / 2) + 1), so even dividers differ from
the time base's D + 1), one 16-bit unit = `scale_q24` / 2^24 codes for the dBV term, and the zero code moved into the
16-bit domain; the first info line then carries the filter's order as a short tag ("CIC3"; owner: no N on the screen,
READ_SPECTRUM has it) and the orange aliasing warning is not shown. Not benched (the bitstream is not flashed yet).

Waterfall and density (fw 0.2.32) show one channel (CH1 unless only CH2 is selected or on). Waterfall: each new
capture adds one row at the top (the plot holds 336), coloured over the plot's 8 divisions from the bottom line
(black) to the ref level (white), the colour legend on the second info line, the time the rows span on the first.
Density (fw 0.2.35): each capture's spectrum line adds hits to a 720 x 336 grid. A cell keeps an 8-bit count and the
16-bit index of the capture of its last hit; a hit decays the count to now (exp(-age / 16 captures), Q8 table) and
adds 32 (saturating), drawing decays it the same way, so the persistence is uniform in time and position with no
fading pass (0.2.33's one-stripe-per-capture fade showed as a band sweeping down the screen). A cell hit in every
capture saturates white; one hit in 10 % of captures sits around dark red; a cell 256 captures old is cleared when
drawn. Colour ramp (legend "rare or old" .. "frequent" on the second info line): black, dark blue, red, yellow,
white. The level grid and labels stay.
Both histories clear when anything that changes what a column or level means changes (FFT length, window, span,
start, ref, scale, sample rate, channel), so a view never mixes axes; in STOP they wait for RUN and say so. The
rows are stored as RGB565 (drawn with memcpy bursts) and the counts as words with zero words skipped, since every
uncached pixel access costs about 90 cycles. Memory: 484 KB waterfall + 726 KB density (.bss).

Axis cap (fw 0.2.36, owner): the view shows at most `PC_SPECTRUM_FMAX_HZ` = 25 MHz (spectrum.h), or fs/2 when that
is lower; at 200 MS/s the full span is 0-25 MHz and zoom, pan and Auto stay inside it. The front end rolls off near
30 MHz, so above 25 MHz there would only be artefacts. The info line still states the sample rate, and READ_SPECTRUM
still returns every bin up to fs/2.

Gestures on the plot (fw 0.2.35, the scope view's rules from `scan_for_touch` in statemachine.c): a touch that stays
within 2 px and ends within 200 ms is a tap; left of the plot centre zooms the span out one step (x 2), right of it
zooms in (/ 2, down to 1/64 of the Nyquist range), keeping the centre, like the time/div taps in the scope view. A
touch that moves more than 2 px is a drag: the spectrum follows the finger horizontally (start frequency, in
1/65536 of the Nyquist range) and is redrawn while dragging; at full span there is nothing to pan. Start, centre and
stop frequencies are the labels under the plot.

Header and averaging (fw 0.2.40-0.2.42, owner): one header line, left to right: per shown channel its peak "CH1 ^
977 Hz +2.7 dBV" in the channel colour, "RBW 732 Hz", the decimation filter tag ("CIC3"; the averaging is not repeated,
the strip's Average field shows it) and, in the waterfall, the time its rows span; the colour legend sits at the right end in the
history views. No fs or N (they are the time base and the FFT-length field). Warnings appear centred over the top of
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

Span and sample rate (fw 0.2.39, owner: like a spectrum analyser): the span drives the scope's time base (the same
setting the scope view uses, short time bases only). A zoom-out tap with the whole axis shown steps to the next
faster rate, until fs/2 reaches 25 MHz (50 MS/s, 2 us/div); a zoom-in tap at a span of fs/16 or less (N/16 bins; fw 0.2.41, 0.2.39 used 64 bins) steps
to the next slower rate instead, keeping the centre frequency and halving the span in Hz, provided the new axis
still holds the window (otherwise the plain zoom continues to 1/64 of the axis). After a rate change the view waits
for the first capture at the new rate ("Waiting for a capture at the new sample rate") and the histories clear.

Persistence (fw 0.2.39): FFT length, window, scale and ref level are saved in settings-sector words 208 (0x5E00
marker, window, scale, length) and 209 (ref, int16), with everything else, by 1.00o5's power-off interrupt (the only
place 1.00o5 writes the settings sector, apart from a failed load; not in the picture / waveform viewers, not on
ENTER_FEL or a battery that dies). Not restored: the display mode (the scope boots into the scope view), channel,
span, start and view. The Filter choice (fw-decim, word 207) is saved the same way.

Auto (fw 0.2.42, owner: deterministic, identical presses give identical settings): a fixed probe procedure that
does not depend on the state it starts from, about 1-2 s, "Auto..." in the field and the plot meanwhile, any touch
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
4. Derive from (f0, f_hi) only: span = max(1.25 f_hi, 20 f0), at most 25 MHz; rate = the slowest whose fs/2 >= 1.25
   span; with f0 > 0 slower still until RBW = 1.5 fs / N <= f0 / 4, the span then trimmed to 0.8 fs/2.
5. Confirm with 2 records at that rate; if the band seen there is less than half the probe's (the probe sees edges
   and noise far up) and that asks for another rate, one refinement with 2 more records.
6. Final: ref = the first multiple of the scale at least 3 dB above the peak, scale 5 / 10 / 20 dB/div by whether
   peak - floor + 10 dB fits 8 divisions, span from 0 (the narrowest zoom step holding it), averaging 4.
Expected for the 1 kHz square at depth 3000 (N = 2048): probe f0 = 0, the search finds 1 kHz at 500 kSa/s, the
RBW rule (<= 250 Hz) picks 200 kSa/s, span about 80 kHz: the comb fills the plot. At depth 6000 (N = 4096) 500 kSa/s.

Warnings (fw 0.2.42, owner: consequence and action, one short line each, centred over the top of the plot in orange,
only while they apply; "MSa/s" everywhere as in the top bar):

| warning | when |
|---|---|
| CHn overrange: increase V/div | the shown channel's last readout clipped |
| Aliasing possible: set Acquisition > Filter | below 200 MSa/s with the Filter off (0x1534+); "use a faster time base" without the filter |
| Stopped: press RUN to fill the view | stopped in a history view, or no record yet |
| Auto needs RUN: press RUN / Auto needs trigger mode Auto or Normal | Auto pressed while stopped / in single mode |
| Waiting for a capture at the new rate | after a span-driven rate change, until the first record at the new rate |
| No trigger: check the trigger level | the latest record was an auto time-out (fw 0.2.41) |
| No channel enabled: switch on CH1 or CH2 | centre of the plot, both channels off |

Levels are dBV of a sine's RMS (a tone centred on a bin reads its RMS level on every window; noise reads per bin,
divide by ENBW for density). Volts per code use the calibration chain of protocol section 8.5, probe factor included
[U: T8]. Number formatting is host-tested (`host_tests/test_spec_fmt.c`). Settings live in RAM (defaults: CH1, 2048,
Hann, 10 dB/div, ref 0 dBV, full span). Protocol: GET_STATUS byte 43 = display mode, READ_SPECTRUM 0x020A returns
the displayed spectrum (protocol section 14 row 45).

Costs: FFT per channel per new capture, measured with FFT_BENCH on 0.2.31: 2048 points 4.9 ms from DDR, 2.3 ms from
on-chip SRAM, 4096 points 10.9 ms (+ about 1-2.5 ms dB conversion). Since 0.2.33 the view runs N <= 2048 from SRAM A
(0x1000-0x7008, the space FFT_BENCH borrows; nothing else uses it at run time, all stacks are in DDR). Drawing: one
728 x 432 clear (memset bursts since 0.2.32) and blit per pass, as 1.00o5's trace view.

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

Owner: the cost of a longer record shows where it is chosen. Under the acquisition menu's "Memory" header the menu
shows "pass NN ms", the main-loop pass time (GET_STATUS `pass_ms`) of the last pass before the menu opened. The menu
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

| | `PC_PROTOCOL=0` | `PC_PROTOCOL=1` |
|---|---|---|
| image (GCC 13 / GCC 10) | 348,672 / 346,112 B | 378,880 / 376,320 B |
| end of `.bss` (GCC 13) | 0x8057F738 | 0x80590A81 |
| free DDR up to the stacks at 0x81FB5000 | 26.2 MiB | 26.1 MiB |

The next fixed DDR user above `.bss` is the bootloader's copy of SD sector 710 at 0x81BFFC00,
22.4 MiB above the new end of `.bss`. The image needs 740 SD sectors from sector 750, so it ends
at sector 1489, well below the FAT partition start at 2048 or later.

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
the brief error the owner saw at every boot. Since 0.2.17 the protocol build probes for the file silently and
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

The owner's 25Q80 now holds Atlan's AL3 bitstream (0x1532, variant 2; written and read back
2026-10-18, power-cycle check of the diagnostics screen pending). The expected values below are
for 0x1532. The stock bitstream (0x1432, variant 1) gives `fpga=0x1432`, ring 4096, caps 0x7C00,
and E_UNSUPPORTED for the generator and for geometry other than 1500/3000. The owner also saw
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
   answer with `ui_busy` = 1, and SET_CHANNEL must give E_BUSY.
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
| 108-511 | 0 (not covered by the checksum; every save rewrites the whole sector from zeros) |

**Sector 709** (settings, u16 words, checksum of words 2..255 in words 0-1, version 0x0100/0x0015 in words 2-3)
holds the base calibration results: words 168-173 CH1 `dc_calibration_offset[0..5]`, 174-179 CH2, 184-187
`adc1compensation` / `adc2compensation` of CH1 then CH2 (i16). They are taken from the loaded 709 buffer only
when sector 708's checksum is valid; otherwise defaults (PWM 860, compensation 0, `signal_adjusters`, 174/220/385)
load and the input calibration is offered again. The diagnostic view's RESTORE reloads them (and 708), RESET sets
the defaults in RAM.

**Where the compensation is applied.** `fpga_read_adc_data` (every UI readout, READ_TRACE, the calibrations): per
core `sample = raw + compensation`, clamped to 0..255, overload flags, and in the second (core B) pass a clip-matching
rule that copies one core's value to the other near 0 and 255 so the two saturate at the same code. Then, for
min/max/average/RMS only (not the trace buffer), `+ dcoffset x 100 / dc_shift_value` (a soft-division per sample).
The decimation path (0x1534, K >= 1) adds `(adc1compensation + adc2compensation) / 2` to every 16-bit sample
converted to a code (`pc_glue_decim_read`). `fpga_average_trace_data(_long)` (test.c only) adds core A's value.
READ_RAW is raw.

**Why the per-core spread survives on the owner's unit.** CH1's raw core zeros differ by 5.35 codes (A 130.93,
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
