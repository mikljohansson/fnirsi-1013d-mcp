# Repo analysis: Donwulff/FNIRSI_1014D_Firmware (branch atlan4-base) + related repos

Analyst: repo-donwulff. Analysis date 2026-10-18.

Read in full: CLAUDE.md, AGENTS.md, README.md, BOOT_NOTES.md, FPGA_NOTES.md (744 lines), PORT_AUDIT.md (1057 lines), ROADMAP.md,
fpga/README.md, bootloader_1014d/README.md, tools/acqprobe_analyze.py, tools/check_*.py.
Skimmed: REVIEW-2026-08-21.md (1703 lines; headings, summary, 1013D-relevant items) and PORT_A.md (historical).
Code read: port_config.h, fpga_control.c/.h, variables.c/.h tables, PC_interface.c/.h, cdc_class.c, usb_interface.c (CDC part),
scope_functions.c (acqprobe), Makefile, sd_card_layout.h.

Not done: nothing was built or run. There is no arm-none-eabi-gcc on this Windows box, so the host test suite was not run.
The firmware clone is shallow (one commit, 6868ac1 of 2026-10-02), so there is no git history.

Local paths
- D:\src\FNIRSI_1014D_Firmware (main subject, was pre-cloned)
- D:\src\FNIRSI-1013D-1014D-Hack-donwulff (Donwulff's notes repo, depth 1, has notes.md)
- D:\src\Anlogic_AL3-10_Analyzing (pecostm32 bitstream RE, depth 1, 236 MB)
- WARNING: D:\src\FNIRSI-1013D-1014D-Hack (no suffix) is pecostm32's repo cloned by another agent. It is NOT Donwulff's and has no notes.md.

---------------------------------------------------------------------------------------------------------------------------
## 1. Repo structure, branches, build, hardware status

### Lineage
- pecostm32/FNIRSI_1013D_Firmware (open 1013D firmware, GPLv3)
- then Atlan4/Fnirsi1013D v1.00o5 (evolved 1013D: new menus, signal generator, USB CDC, RTC, 35-entry timebase space with roll mode, replacement-FPGA support)
- then this repo, branch `atlan4-base`: Atlan4 v1.00o5 vendored at commit 9daa91f, with pecostm32's official 1014D modules grafted on.
- Donwulff did the first quick 1014D port in 2023-01 (branch PORT_A).
- Branches: `atlan4-base` (active, the only one cloned), `main` (pristine pecostm32 1013D), `PORT_A` (2023 port, superseded).
- Heavily AI-agent authored (Claude Code, opencode "Big Pickle", Grok Build). Docs say "trust the code" over the docs.

### Layout (tracked)
- `fnirsi_101xd_scope/`: THE firmware project (renamed from fnirsi_1013d_scope on 2026-08-21). NetBeans makefiles. About 87k lines of C.
  Biggest files: menu.c 9.8k (1013D touch UI), scope_functions.c 8.6k, statemachine.c 6.6k (1013D touch state machine),
  menu_1014d.c 5.6k and sm_1014d.c 3.0k (1014D UI), usb_interface.c 1.75k, fpga_control.c 1.6k, ff.c (FatFs).
- `bootloader_1014d/`: source-built 1014D SD loader (from pecostm32 fnirsi_1014d_startup, commit b9ea2ae). 1014D only.
- `fpga/AL3_1014D/`: Anlogic TD project, Atlan4's AL3 `zaklad.v` retargeted to the stock 1014D pinout. Built, never flashed.
- `tools/`: acqprobe_analyze.py, check_bootloader.py (eGON header, checksum, size <= 0x7000), check_sd_layout.py, tests/.
- `fnirsi_1013d_sd_card_bootloader/`, `fnirsi_1013d_startup_screen/`, `fnirsi_1013d_startup_from_sd_card/`: pecostm32's three 1013D loader trees.
  Provenance only. "Never run make" (startup_screen post-build hardcodes an old path). Neither committed bootloader binary comes from them.
- Docs: CLAUDE.md, AGENTS.md, README.md (pecostm32's original README appended), BOOT_NOTES.md, FPGA_NOTES.md, PORT_AUDIT.md, ROADMAP.md, REVIEW-2026-08-21.md, PORT_A.md.
- Misc: V0.005_Windows.7z (external, unverified), configuration_file.txt (display config sector 710 format),
  waveform_file_format.txt (800-byte settings, then 3000 B CH1, 3000 B CH2), versions.txt, power_points*.jpg.
- NOT in the clone (untracked local reference trees the docs cite): Atlan4-1.00o5/, Atlan4-FPGA/ (zaklad.v, "FPGA commands.txt" stock-protocol reference,
  stock-1013D flash dump fpga-1013D-1-zb25vq80.bin), pecostm32-RE/, Bootloader fw0.02-0.04/, donwulff-notes.md, bench/.
  fpga/AL3_1014D/ does carry a patched copy of zaklad.v and the PLL/memory IP.

### Variant switch
- `fnirsi_101xd_scope/port_config.h`: `#define PORT_1014D 1` (default) or `0` (1013D). Also `PORT_A_KEYDEBUG` (boot-stage squares and heartbeat overlay).
- Both variants compile the same explicit object list (nbproject/Makefile-Debug.mk). Separation is by `#if PORT_1014D`, in three forms:
  (a) whole-file guards: menu_1014d.c, sm_1014d.c, fonts_1014d.c compile empty on 1013D;
  (b) per-function stubs: touchpanel.c, DS3231.c, power_and_battery.c are stubs on 1014D;
  (c) shared always-compiled files with dead sections: menu.c, statemachine.c, generator.c, PC_interface.c, uart.c, clock_synthesizer.c.
  Never add whole-file guards to group (c).
- Gotchas: guards must come AFTER includes (variables.h pulls port_config.h). `#ifndef PORT_1014D` is always false (the macro is always defined; F3).

### Toolchain and headless Linux build
- `arm-none-eabi-gcc` (GCC 14 works). CFLAGS: `-mcpu=arm926ej-s -O2 -mfloat-abi=soft -nostdlib -lgcc -DNO_STDLIB=1 -fcommon -fno-builtin-strcpy -fno-stack-protector -Wno-error=implicit-*`.
  Linker script fnirsi_101xd.ld. Only config `Debug` is real (`Release` is a stale STM32 leftover).
- `cd fnirsi_101xd_scope && make`, which does: objcopy to .bin, `./mksunxi` (fixes the BROM eGON header), then
  `./flashfilepacker -o fnirsi_101xd.bin -i fnirsi_101xd_scope.bin -l <offset>` to overlay the scope onto a bootloader base image.
- mksunxi and flashfilepacker are opaque committed x86-64 Linux ELFs (executable on this branch). Their sources are NOT in the tree (they live in pecostm32's repos).
  Build host needs x86-64 Linux, python3, GNU make.
- Offsets: 1014D uses `../bootloader_1014d/build/bootloader_1014d_base.bin` at 0x8000 (scope at SD sector 80).
  1013D uses the committed `bootloader_base.bin` (375,808 B, Atlan4 loader v0.8 incl. updater) at 0x5BC00 (SCOPE_START_SECTOR 750).
- The Makefile echoes `[port_config.h variant: v1.00o5-1013D|-1014D]` and `>>> BOOTLOADER: ...`. Check them before flashing.
  It regenerates build_time.h every make and deletes the other variant's stale copies.
- Outputs in dist/Debug/GNU_ARM-Linux/: fnirsi_101xd.bin (SD image), fnirsi_101xd_scope.bin (RAM/FEL image), plus variant copies fnirsi_1013d(.bin|_scope.bin).
- Tests: `PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover -s tools/tests -v` (5 tests).
  They compile selected firmware functions verbatim with hardware mocks and UBSan (host GCC plus ARM GCC), both variants, plus loader readiness/menu logic.
  They do NOT cover MMIO, timing, USB or SD. Makefile `test` targets are empty NetBeans stubs. Ignore compile.sh/burn.sh/debug.sh (stale STM32).
- SD install: back up the whole card, MBR with all partitions at >= sector 2048, FAT32, then `dd if=fnirsi_101xd.bin of=/dev/sdX bs=1024 seek=8`.
  The author builds on a Linux server and flashes from a Windows laptop.
- RAM load (no SD write): `sunxi-fel -p write 0x7FFFFFE0 fnirsi_101xd_scope.bin exe 0x80000000`. DRAM must already be initialised by the running scope or loader.

### What has run on hardware vs not
- Real hardware: the author's 1014D only (stock FPGA 0x1432). SD boot, Si5351 init, traces, UART keys, menus, USB mass storage work. Many fixes from 2026-07 to 10 are "implemented, hardware verify pending".
- **1013D variant: compiles, never hardware-tested from this tree** (CLAUDE.md, README, PORT_AUDIT 5e). The author owns no 1013D.
  Policy since 2026-08-21: the 1013D is deliberately left at Atlan4 v1.00o5 behavior wherever 1014D fixes could have been extended
  (a known divergence from pecostm32's 1013D upstream).
- Custom FPGA bitstream (fpga/AL3_1014D/out/FPGA_1014D.bin): never flashed. New 1014D loader: host-tested only, not installed on hardware.
- Open checklists: PORT_AUDIT section 4, 5f (hardware acceptance), bootloader_1014d/README.md verification.

---------------------------------------------------------------------------------------------------------------------------
## 2. MCU-FPGA protocol, capture geometry, FPGA versions, boot, flashing

### Bus (fpga_control.h/.c)
- Bit-banged on Allwinner F1C100s Port E. Data PE0-7 (cfg reg 0x01C20890). PE8 = clock (idle high; one write = pulse low then high).
  PE9 = command/data select, PE10 = read/write select (ctrl cfg 0x01C20894, data reg 0x01C208A0).
- FPGA pins: i_mcu_clk P1, i_mcu_rws P2, i_mcu_dcs P3, data P135-138 and P141-144. Identical on 1013D and 1014D.
- Select macros on PE9/PE10: CMD_WRITE 0x600, CMD_READ 0x400, DATA_WRITE 0x200, DATA_READ 0x000.
- Transaction: one command byte, then 0..n data bytes, MSB first (write_byte/short/int = 1/2/4 B; read_byte/short).
- FPGA latches on i_mcu_clk posedge. A multi-byte state machine exists only for cmds 0x0D, 0x0E, 0x1F, 0x32, 0x35, 0x38, 0x3C (write) and 0x06, 0x14, 0x41 (read).
- Sample reads (0x20-0x23) auto-increment the FPGA read address on every byte read.
- MCU: F1C100s ARM926EJ-S at 600 MHz (stock 1014D firmware uses 575 MHz), 32 MB DRAM at 0x80000000.

### Version detection (fpga_get_version(), cmd 0x06, two bytes)
| word | fw_FPGA | meaning |
|---|---|---|
| 0x1432 | 1 | stock FNIRSI bitstream (1013D and 1014D; confirmed on a 1014D) |
| 0x1532 | 2 | Atlan4/"PECO" replacement, Anlogic AL3 (zaklad.v returns 0x15,0x32) |
| 0x1632 | 3 | Atlan4/"PECO" replacement, Anlogic EF2 |
| other | 0 | unknown; most fw-dependent code takes neither branch |

- There is NO 0x2xxx or 0x3xxx version word anywhere in this repo (grep is clean).
- Docs sometimes call 0x1532/0x1632 "pecostm32" and sometimes "PECO/Atlan4". PECO is Atlan4's firmware branding; the designs are Atlan4's zaklad.v.
- Atlan4 ships three FPGA families: AL3 (TD 4.6.8 SP1), EF2L45 (TD 4.6.8 plus an stm32f103 JTAG programmer), Altera EP4CE06 (Quartus 22.1, CH341, rbf2bin).
  Only the AL3 family was vendored and studied here.

### Command inventory
Stock (0x1432):
- 0x01 sample-system reset (1 assert, 0 release). 0x04 enable system (0x01). 0x05 read reset-state flag bit0 (becomes 1 about 7 write-clocks after reset).
- 0x06 version. 0x0A read done flag (stock: not sample_write_enable). 0x18 read "triggered".
- 0x0D 32-bit sample-rate divider N. Per-ADC clock = 100 MHz/(N+1), the two interleaved ADCs give 200 MHz/(N+1) per channel. N=0 is 200 MSa/s.
  See table below. Roll mode always writes 0x7D0 (2000, about 100 kSa/s).
- 0x0E 32-bit time base: the auto-trigger timeout counter (fw1 uses timebase_settings[]: 1800 long, 5000..411100 short; fw2/3 write samplecount).
- 0x0F trigger system: 0 = enabled, 1 = disabled (the stock model sets sample_read_enable).
- 0x14 read trigger address (2 bytes). Firmware masks 0x1FFF. Stock hardware address is 12 bit.
- 0x15 trigger channel (0/1). 0x16 edge (0 rise, 1 fall). 0x17 level (8-bit ADC code). 0x1A mode (0 auto, 1 normal/single).
- 0x1F write 2-byte read pointer (stock 12 bit).
- 0x20/0x21 = CH1 ADC A/B memory read. 0x22/0x23 = CH2 ADC A/B memory read. 0x24-0x27 = live ADC pin reads (roll mode uses 0x24/0x26).
- 0x28 mode select: 0x00 fast (< 100 ms/div), 0x01 roll (>= 100 ms/div).
- 0x29 dual-ADC mode (pecostm32 addition, absent from stock captures, harmless).
- 0x32/0x35 CH1/CH2 DC-offset (16-bit PWM duty). 0x33/0x36 attenuator relay code 0..5. 0x34/0x37 AC/DC. 0x38 backlight PWM (16 bit).
- 0x3C write / 0x41 read: 16-bit loopback register ("touch_panel_address"). Stock firmware writes 0x3C and hangs if 0x41 does not read 0x8150. Open firmware never sends 0x3C.
- 0x64-0x6E "special IC" I2C bridge (1014D stock firmware only; the open firmware never sends them).

Atlan4 replacement FPGA only (fw_FPGA >= 2): 0x0B pretrigger samples (value/2), 0x0C total samples (value/2), 0x1B hold-off.
Generator 0x50 on/off, 0x51 frequency (phase increment = f * 2^32 / 50e6), 0x52 duty. The firmware sends 0x50-0x52 at every boot on both variants even on the stock FPGA (stock tolerates it).

Other facts:
- Per-channel command bytes come from CHANNELSETTINGS (enablecommand, couplingcommand, voltperdivcommand, offsetcommand, adc1command, adc2command).
- fpga_set_channel_voltperdiv clamps to 5, so 50 mV/div (index 6) is software zoom of the 100 mV/div hardware range (about 7 bit effective).
- Backlight PWM lives in the FPGA (brightness = (0..100) * 560 + 4000).
- Relay encoding in the 1013D FPGA model: relay1 = bit0, relay2 = ~bit1, relay3 = ~bit2, only accepted if code < 6.

Sample-rate table (timebase to rate, fw1, from time_per_div_sample_rate[] and sample_rate_settings[]):

| div | rate idx | N (0x0D) | rate per channel |
|---|---|---|---|
| 5 ns to 500 ns | 0 | 0 | 200 MSa/s |
| 1 us | 1 | 1 | 100 MSa/s |
| 2 us | 2 | 3 | 50 MSa/s |
| 5 us | 3 | 9 | 20 MSa/s |
| 10 us | 4 | 19 | 10 MSa/s |
| 20 us | 5 | 39 | 5 MSa/s |
| 50 us | 6 | 99 | 2 MSa/s |
| 100 us | 7 | 199 | 1 MSa/s |
| 200 us | 8 | 399 | 500 kSa/s |
| 500 us | 9 | 999 | 200 kSa/s |
| 1 ms | 10 | 1999 | 100 kSa/s |
| 2 ms | 11 | 3999 | 50 kSa/s |
| 5 ms | 12 | 9999 | 20 kSa/s |
| 10 ms | 13 | 19999 | 10 kSa/s |
| 20 ms | 14 | 39999 | 5 kSa/s |
| 50 ms (sweep) | 15 | 99999 | 2 kSa/s |
| 100 ms (sweep) | 16 | 199999 | 1 kSa/s |
| 200 ms (sweep) | 17 | 399999 | 500 Sa/s |

Timebase space is 35 entries: index 0 = 50 s/div, 34 = 5 ns/div; indices < 11 are "long" (roll) in Atlan4 numbering.
On the 1014D roll is kept only for <= 500 ms/div; 200/100/50/20 ms are sweep-only. The 1013D keeps Atlan4's mapping.

### Acquisition sequence, stock FPGA (fpga_do_conversion / scope_acquire_trace_data)
1. 0x0F (0 if trigger enabled, else 1).
2. 1014D build only, fw1: 0x29 <- 1, then 0x28 <- 0 (F25). **Not sent on the 1013D build.**
3. 0x01 <- 1 (reset). 4. Poll 0x05 bit0, twice (second read is pecostm32's glitch check, F26), each bounded to 2,000,000 reads.
5. fw_FPGA > 1 only: 0x0B, 0x0C, 0x0E. 6. 0x01 <- 0 (release).
7. Poll 0x0A bit0 (non-blocking in the main loop via display_data_done). 8. 0x14 gives trigger address T.
9. fw1 read pointer = (T < 750) ? T + 3345 : T - 750 (a wrap at 4096). Then 0x1F <- ptr, 0x20, 1500 bytes (ADC A); 0x1F <- ptr, 0x21 (ADC B); interleave into 3000 samples per channel. Same for 0x22/0x23.
- Per-ADC compensation (adc1compensation, adc2compensation) is applied at all rates.
- Buffers: MAX_SAMPLE_BUFFER_SIZE 16382 bytes per channel. Default FPGA total samples 3000, trigger point 1500, minimum total 80.

### Capture geometry (bench-measured on the 1014D, stock FPGA, with acqprobe 2026-07-17 to 19)
- Ring is **4096 samples per ADC**. Four rings (2 channels x 2 ADCs) = 16 KB, plus an AWG block on the 1014D.
  Pointer arithmetic is strictly modulo 4096 (b[k] == b[k+4096] byte-exact; fails at 4095, 4097, 512, 1024, 2048).
- The first byte after a 0x1F pointer write is a stale pipeline byte (off by +/-1 code, reproducible). Raw readers must discard sample 0.
- Normal readout uses only about 37 % of the ring: window = T-750 to T+750 ring addresses per ADC, so 1500 x 2 ADCs = 3000 interleaved samples per channel, trigger mid-window (50 %). About 2600 per ADC are unread.
- 0x1F takes any pointer. The same capture can be re-read repeatedly and is byte-identical (ring static once 0x0A is set). So full-ring dumps and offset windows need no FPGA change.
  A disabled channel's ADCs keep sampling: all four rings are always live.
- Write behavior: each arm starts the write head at a fixed ring position (about 0), fills forward through the trigger to a variable stop, and leaves the rest untouched.
  Pre-trigger fresh data about 910 ring addresses (750 in window). Post-trigger fresh about 2507 addresses (steady trigger); 1705 for a noisy trigger.
  Unwritten gap 679 to 1481 addresses, beginning about 950-1750 past the window's right edge. The constant 3345 is only a display offset (4096 - 751), not a fill length.
- Trigger is a digital comparator on the ADC sample stream (8-bit level), so trigger time is quantized to the sample clock. Sub-sample phase is random per capture (the basis for the random-interleaved-sampling idea).
  scope_process_trigger() re-finds the crossing near buffer centre (steps of 2 to stay on one ADC parity); the display anchors on disp_trigger_index.
- Raw ADC A-B DC offsets inside the rings: about 1.4 to 2.5 codes (CH1), 4.9 to 5.5 codes (CH2, disabled).
- Rates (measured): the normal read path costs 2.6-3.1 us/sample (MCU-bound), giving 90-128 captures/s at every timebase tried.
  The raw fpga_dump_ring loop moves 23,040 B in 7-8 ms (0.30-0.35 us/sample, about 3 MB/s), about 10x faster.
  Arm-only rate: 21 k/s at 1 us/div (about 47 us MCU overhead per arm), 3.8 k/s at 100 ns/div (0x28 path adds 0.22 ms), 295/s at 100 us/div (fill-bound).
  Estimated 500-900 raw-window captures/s; full-ring 4-ADC about 150-200/s.
- The stock-FPGA hardware model (pecostm32 Original_1013d.v, section 7b) agrees: 12-bit write and read addresses, trigger checking starts after address > 1600, trigger_read_address preloaded to 800.
  Atlan4's replacement design uses 8192 x 32 bit sample memory (32 of 48 BRAM9K) with 13-bit addresses.

### Sampling clock and Si5351 (1014D only; irrelevant to 1013D hardware)
- 1014D: MS5351M on PA0/PA1 bit-banged I2C. PLLA about 800 MHz, CLK0 200 MHz (generator), CLK1 50 MHz (to FPGA clock pins P23/P24). The 1013D FPGA self-clocks from a 50 MHz crystal on P23 via internal PLL to 200 MHz.
- Runtime presets 50 / 57.1 / 66.7 / 80 MHz (CLK1 = 800/(2*p1b+4)), Factory settings > Sampling clock. 80 MHz (320 MSa/s) glitched. Fabric and ADC ceiling is about 300 MSa/s. Not persisted.

### Stock vs Atlan4 FPGA, pin differences between boards
- 1013D stock (Original_1013D.adc, AL3A10LG144C7, LQFP144): ADC1A P66..54, ADC1B P67..74, ADC2A P91..84, ADC2B P98..106; encodes P75/P53 (ADC1 A/B), P110/P83 (ADC2 A/B);
  offsets P112 (CH1), P111 (CH2); relays CH1 P121/P124/P126, CH2 P128/P125/P127; AC/DC P129 (CH1), P132 (CH2); backlight PWM P42; **1 kHz probe-comp output P133**; crystal P23.
- 1014D stock (Original_1014D_fpga_generated.adc, same die): MCU bus identical; analog side reshuffled; backlight P7; clocks i_clk1 P23 and i_clk2 P24; 8-bit DAC bus P38-50 (AWG); I2C master P33/P34 to the "special IC"; AC/DC P132 (CH1), P133 (CH2).
  Atlan4's zaklad.adc is 1013D-pinned; flashing it unmodified to a 1014D would drive wrong pins (his P133 is the 1014D CH2 AC/DC).
- fpga/AL3_1014D zaklad.v deltas vs Atlan4: o_1khz_calib deleted, o_dac_d parked at 0x00, I2C tristated, i_clk2 unused.
  Its decoder covers everything fw_FPGA==2 sends; 0x0F is inert; 0x24-0x27 read live ADC pins; 0x28/0x29 ignored.
- The scope CPU cannot reflash the FPGA (config lines do not reach the F1C100s; no ISP core in the stock netlist).

### FPGA identity facts for 1013D units (important for this project)
- Donwulff's notes cite a CNX teardown showing an **EF2L45LG144B** (Anlogic EAGLE) in a 1013D. Older 1013D units used Altera Cyclone IV **EP4CE6**. pecostm32's own 1013D has **AL3A10LG144C7** (Anlogic AL3_10); his TD project targets that part.
- PORT_A.md calls EF2L45 "Gowin", which is wrong; EF2 is Anlogic. "Different models and builds use different FPGAs", hence Atlan4's three trees.
- The version word is not family-specific, so the user's unmarked 144-pin part must be identified physically or by boot behavior.

### Flashing routes and risks (FPGA SPI flash)
- 1014D schematic: "FPGA Configuration scheme is Active Serial 3V3". SPI flash (schematic W25Q80; the author's unit has a ZB25VQ80ATIG, unknown to most flashers), 6-pin header J2 `FPGA_FLASH`, JTAG header J5, special-IC header J3.
- Bench-proven read: Bus Pirate plus recent flashrom in generic mode, 3V3 pin unconnected, board powered. J-Link failed (Winbond-only SPI support). CH341A on J2 is the alternative. TD JTAG is capped at 400 kbps.
- Atlan4 routes: AL3 = CH341 plus 3.3 V level converter on the FPGA flash; EF2 = stm32f103 JTAG programmer; Altera = CH341 plus rbf2bin.
- Gates before any custom bitstream: new loader installed and tested (old loaders hang forever on non-0x1432); verified stock flash backup (1014D: pecostm32 flash_fpga_1014d.bin, 1 MB; 1013D: Atlan4's fpga-1013D-1-zb25vq80.bin, 1 MB, not in this clone);
  first power-on with nothing on the BNCs (relay polarity unverified); run Base calibration immediately (constants are Atlan4-1013D tuned).
- Loader F2 (stock CPU firmware) does NOT restore a stock FPGA. A custom FPGA with stock CPU firmware is not a valid fallback.

### Boot chain, sector-710 boot-byte contract, FEL recovery
- BROM loads the eGON loader from SD byte offset 8192 (sector 16). If SD boot fails it falls back to SPI NOR, where the stock FNIRSI firmware still lives (loader F2 boots it from SPI 0x27000).
- 1014D loader (bootloader_1014d): clocks, DRAM, display, UART1 key controller, FPGA init, then a bounded readiness check (10,000 version reads, about 1000 NOPs apart) accepting 0x1432 or 0x1532; on failure it enters FEL automatically.
  The key controller returns 49 when idle and 0 when an extra key is held; then the menu is F1 = SD firmware (labelled "PECO"), F2 = stock SPI firmware, F3 = FEL. No key means SD firmware. Scope loaded from SD sector 80 (eGON.EXE header) to 0x80000000.
- **Sector-710 boot-byte contract (Atlan4/1013D chain only).** Byte at SD sector 710, offset 0x1F, mirrored to DRAM 0x81BFFC1F by the loader. Values: 0 = PECO (SD), 1 = FNIRSI (SPI), 2 = FEL; value > 3 (bit 0x04) shows the menu, otherwise & 3 selects.
  The sector checksum covers only the first words, so the byte is poke-safe. Other sector-710 content: magic AAAAAAAA/55555555 (words 0,1), TCON timing (2,3), magic CCCCCCCC/33333333 (4,5), checksum of words 0-5 (6).
  Panel timing falls back to hardcoded 0x041E0044/0x041A0017 when the magic is invalid.
- Alignment quirk: the scope declares the config pointer as uint32* at unaligned 0x...1F. ARMv5 LDR rotates (reads work) but STR writes the aligned word at 0x...1C (clobbers the touch X-swap byte, zeroes the boot byte). Fix: use uint8*.
  Atlan4's fw0.04 loader (touch-driven menu, uint8* byte, reads sector 710 into DRAM) is the complete reference. The shipped v0.8 exists only as a binary.
- **The 1014D loader never reads sector 710.** The old "poke byte 363551 to enter FEL" trick is dead there. On the 1013D chain (committed bootloader_base.bin = Atlan4 "BOOT fw v0.8") it should still apply.
- Bootloader v0.8 contains the fpga_check_ready() 0x1432 spin in its SPL (about file offset 0x1340) and main stage (about 0x125dc), but with zero call sites (verified twice by disassembly). So v0.8 boots with any FPGA version word.
  That is the real meaning of Atlan4's "install bootloader v0.8 or higher first". Older loaders (fw0.02-0.04, pecostm32 startup) hang forever on a non-0x1432 FPGA.
- SD map, 1013D (unchanged): sector 16 loader, 708 input calibration, 709 settings (checksummed blob), 710 display config plus boot byte, 750 scope start (image offset 0x5BC00).
  1014D: 16 loader, 80 scope, 2046 calibration, 2047 settings (moved on 2026-09-20 because the app outgrew 708/709; one-time FEL migration required). check_sd_layout.py enforces the 1014D layout.
- FEL: 1014D = Factory settings > "FEL firmware update" (direct BROM jump to 0xFFFF0020, I-cache invalidated first) or key held at power-on then F3. 1013D = sector-710 byte 2 or the touch loader menu.
  Host command: `sunxi-fel -p write 0x7FFFFFE0 fnirsi_101xd_scope.bin exe 0x80000000`. Stock sunxi-fel cannot initialise FNIRSI DRAM, so a first-time FEL needs a DRAM-capable stub (pecostm32 Test code/fnirsi_1014d_startup_with_fel).
- Last resort: remove the SD card so BROM boots the stock firmware from SPI NOR. USB MSC exposes the raw SD from sector 0, so a host can `dd` a new image to it.
- Config save: 1014D saves on the key controller's OFF code; the 1013D saves in a power-loss IRQ (unsafe mid-SD-write, noted).

---------------------------------------------------------------------------------------------------------------------------
## 3. Headless Anlogic TD build

- Atlan4 uses TD 4.6.8 SP1 build 116866 (FPGA.al says TD_Version 4.6.116866; project created 2025-11-15). No public mirror exists for it.
- Public alternatives from Sipeed (dl.sipeed.com, registration-free; files >= 10 MB are captcha-gated via /imgCode, /checkVerify, /file/download):
  TD 5.0.3 Linux (build 28716, 414 MB zip), TD 5.0.4 Windows MSI (build 27252, 467 MB), TD 4.6.4 RHEL (fallback). Anlogic's own 4.6/5.6/6.0 downloads are login-gated.
  pecostm32's 1013D project used TD 5.0.28716 (= 5.0.3). English docs: github.com/kprasadvnsi/Anlogic_Doc_English.
- **Licensing**: community file `Anlogic_20251116.lic` ("NODELOCKED license for TD", generated 2025-08-30), HOST_ID=ANY, **FEATURE expiry 2026-08-31**. Today (2026-10-18) that is expired. A newer .lic from the Sipeed folder or from Anlogic customer service is needed.
  Also `Anlogic_20240310.lic`. Install by unzipping TD and saving the lic as `<TD>/license/Anlogic.lic`.
- Linux headless: `bin/td -gui` links libgtk-x11/libgdk-x11 unconditionally, so two empty stub .so files with matching sonames go on LD_LIBRARY_PATH (shim/). Batch mode feeds a Tcl script to `td`. Harmless "sh: Bad fd number" spam (bashisms under dash).
- Batch flow (fpga/AL3_1014D/build.tcl, build.sh [builddir], env TD_HOME and TD_SHIM): `read_verilog` accepts exactly one `-file` and elaborates immediately, so concatenate sources; AL_* primitive models come via `-lib $TD/arch/al3_macro.v`;
  then read_adc, read_sdc, `import_device al3_10.db` (no -package; LQFP144 is the only package), optimize_rtl, map, pack, place, route, `bitgen -bit x.bit -bin x.bin` (the .bin is the flashable SPI image).
  TD 5.0.3 ships arch/al3_10.db (1014D), al3_s10.db (Atlan4's 1013D project), al3_6.db.
- Validation on Atlan4's 1013D project, all defaults: 602 LUT / 458 reg / 32 BRAM9K / 60 IO (shipped 620/471/32/60). .bit 285,324 B vs shipped 285,341 B.
- **Build times are not recorded anywhere in the docs.** The design is small (602 of 8640 LUT, 0 of 3 DSP, 32 of 48 BRAM9K), so minutes is likely.
- Timing (TT 1.10 V 85 C): shipped 4.6.8 defaults 160.4 MHz equivalent on clk_200MHz, TNS -21.1 ns; server 5.0.3 defaults 277.9 MHz, TNS 0; laptop 5.0.4 effort=high 288.6 MHz, TNS 0.
  The 90-degree ADC clock group clkc[1] is negative in every build (-3.3 to -3.7 ns, 144-149 MHz equivalent). Cause is structural: a 32-bit sample_rate_counter compare drives CE pins of the adc*_enc toggle flops across a 1.25 ns window.
  Harmless at 200 MSa/s (enable statically 1), a possible sample-pairing glitch at divided rates. Fix is RTL retiming, not placer effort.
- STA is blind where it matters: sample_write_clock, clk_50MHz and i_mcu_clk are unconstrained and there are no IO delays on the ADC buses. A starter SDC block exists commented out in zaklad_1014d.sdc and fpga/README.md.
- 1014D retarget result: 70 I/O (37 in / 23 out / 10 inout), 32 BRAM9K, FPGA_1014D.bin 282,898 B with the al3_10 id packet `cc55aa33 f0000006 18006c31` and `ec f0 0433` frame marker (1075 frames).
  IOB output-register packing differs across TD versions, so encode skew is build-dependent.
- Free fabric is about 93 %. Free BRAM: 16 x 9K plus two untouched 32K blocks. Ideas listed: AWG wave table, FPGA-side averaging accumulator, envelope decimation, trigger filters, per-ADC gain trim.

---------------------------------------------------------------------------------------------------------------------------
## 4. USB CDC / PC protocol / acqprobe

### USB hardware (usb_interface.c, F1C100s OTG, PIO FIFO mode, DMA off)
- `usb_device_init()`: PHY setup, ISCR force ID and VBUS valid. If `USB_CH340 == 1` it installs `usb_device_CDC_irq_handler`, calls `usb_device_enable()` (200 ms and 500 ms delays); otherwise it installs the mass-storage handler.
- `USB_CH340` is a persisted flag (0 = mass storage, 1 = serial; variables.c:160; saved in the config blob at scope_functions.c ~8224/8444).
- Toggle exists only in the 1013D touch menu (menu.c scope_system_settings_other_settings_CH340_mode_item; statemachine.c:1423 `USB_CH340 ^= 1`). **No 1014D UI entry** (ROADMAP 13). Mode is chosen at USB init.
- Naming is confusing: the usb_ch340_* functions were renamed usb_CDC_*, but the flag kept its name. The endpoints implement real CDC ACM.

### Descriptors (cdc_class.c)
- Device: bcdUSB 0x0200, class 0x02, **VID 0x0483 PID 0x5740** (the STMicroelectronics VCP IDs, so hosts bind stock CDC ACM drivers), bcdDevice 0x0310.
- Config: CDC comm interface (class 0x02/0x02) with header, call-mgmt, ACM and union functional descriptors, interrupt IN 0x81 (8 B, interval 10), data interface (class 0x0A), bulk IN 0x82 and bulk OUT 0x02 (CDC_BULK_MAX_PACKET, 512 in high speed).
- Strings: "FNIRSI", "FNIRSI 1013D", serial "12345678", "FNIRSI Conf", "Custom CDC ACM", "Data Interface".
- SET_LINE_CODING (7 bytes) is accepted on EP0 and ignored.
- Buffers: RX ring 1024 B (usb_rx), TX ring 32768 B (usb_tx), one-byte-free circular scheme.

### Current command protocol (usb_CDC_receive_callback, usb_interface.c ~1480-1590): a debug stub
- Line-based ASCII over CDC. It runs **inside the USB IRQ** (EP2 RX interrupt reads EPFIFO2 into bufferCDC, then parses; the parser masks the EP2 TX interrupt while it runs). Terminator \n or \r, 128-byte line buffer.
- Empty line: prints `#Aktualne hodnoty: `, then `a: <n>` through `k: <n>` (11 debug variables, defaults 310, 320, 70, 50, 1000, 3000, 0, 70, i, 182, 20), then `#DONE`.
- `<letter a..k><decimal>` sets the variable and replies `#value: <cmd>` then `#END`. Side effects: `f` sets fpgasettings.totalsamples (clamped through scope_sanitize_fpga_sample_settings); `g` sets scopesettings.ACQ_trace;
  `e==1` saves a screenshot BMP via scope_save_view_item_file(VIEW_TYPE_PICTURE).
- Anything else replies `#ERR` then `#END`.
- TX: usb_CDC_send_text() and usb_CDC_send_data() only append to the usb_tx ring (send_data busy-waits for space, which is IRQ-unsafe). Nothing leaves until **usb_CDC_in_ep_callback()** is called explicitly.
  That function blocks: it drains the ring in 512-byte packets, waits for TXCSR not-ready, writes EPFIFO2, sets TX_READY. The TX-complete and SOF interrupt paths have the call commented out.
- PC_interface.c: `usb_CDC_process_rx()` is a leftover stub that recognises "#ECHO" and never replies; nothing calls it.
- PC_interface.h defines `sysparam`, a 320-byte packed struct mirroring the stock FNIRSI PC-software blob, and `ini_SysParam()` defaults:
  run_state[0], ch2_enabled[2], time_zoom[20] (2 = 5 ns .. 30 = 10 s), horiz_position[56..58] (3 B LE, centre 1200000), trigger channel/mode/edge/source [64..67], trigger_level[72] (0-255), ch1_volt_zoom[74] (4-13), ch1 position[80..81],
  ch1_coupling[116], ch1_probe[117], ch2 equivalents at [140], [148..149], [180], [181], xy_mode[249], signal_gen[250], selected_channel[284], menu_flags[287].
  Commented-out main() text "#OK,CONNECTED" and "V1.3.0C MOD V9B4" shows Atlan4 tried to mimic the stock PC handshake. **Nothing sends or applies sysparam.**
- Conclusion: there is NO real PC control protocol, no sample-buffer streaming, no generator command over CDC.
- The 1014D build compiles all of this but exposes no way to switch USB_CH340.
- Known CDC defects from REVIEW: `f` set unbounded totalsamples (fixed with a clamp); trace arrays were 2 bytes short of MAX_SAMPLE_BUFFER_SIZE (fixed).

### Gaps against the project goal (PC/MCP control of a 1013D)
- Needed: framed protocol with length and checksum; commands for run/stop/single, timebase, volts/div, coupling, offset/position, trigger, channel enable; sample-buffer pull (raw 0x1F + 0x20..0x23 reads, about 3 MB/s MCU-side ceiling);
  generator control (0x50-0x52 work only on Atlan4's replacement FPGA; the 1013D hardware has no AWG, only the 1 kHz cal output); dispatch outside the USB IRQ; real TX flow control.
- Reusable: CDC descriptors and IRQ handler, circular buffers, acqprobe and fpga_dump_ring, scope_set_*/fpga_set_* setters, the config-sanitize helpers.

### acqprobe (1014D only, `#if PORT_1014D`; Factory settings > Acquisition probe; scope_functions.c ~1890-2215)
- Writes two files to the SD root via FatFs, **overwritten on every run**: `acqprobe.txt` and `ringdump.bin`. Per-run unique names are a queued TODO.
- Requires fw_FPGA == 1 and a short timebase (10 ms/div or faster); otherwise it shows a message and returns. It forces trigger mode AUTO and samplemode 1 during the run and restores them.
  A capture timeout (10M-poll guard) aborts without touching the old files. Short writes or close errors suppress the "Saved" message.
- Phase A: 64 loops (4000 ms cap) of fpga_do_conversion plus wait on 0x0A (arm and capture rate).
  Phase B: 64 loops of the full cycle (plus 0x14 and the channel readout through fpga_read_sample_data, gated on channel enable, with the fw1 pointer translation).
  Phase C: one capture, rawdump = fpga_prepare_for_transfer(), then fpga_dump_ring(cmd, rawdump, buf, 4608) for cmds {0x20, 0x21, 0x22, 0x23, 0x20}; the fifth block re-reads the first for determinism.
- fpga_dump_ring(): write 0x1F with the 16-bit pointer, write the ADC command, set the bus to input, then loop {pulse clock; read PE0-7}, with no compensation. About 0.3 us per byte.
- **acqprobe.txt format**: CRLF lines `label=value`, unsigned decimal. First line `FNIRSI-1014D acqprobe version=1`. Keys in order: timeperdiv, samplerate_idx, samplecount, nofsamples, fw_fpga, clock_p1b, ch1_enable, ch2_enable,
  saved_triggermode, phase_a_loops, phase_a_ms, phase_b_loops, phase_b_ms, dump_ms, dump_bytes (= 5 x 4608 = 23040), raw14 (repeated, first up to 8 values), ringdump_raw14.
- **ringdump.bin format** (23,073 B): magic `ACQP` (4 B); six little-endian uint32 = [format version 1, timeperdiv, samplerate idx, raw 0x14 address the reads started at, blocks = 5, samples per block = 4608];
  five command bytes (0x20, 0x21, 0x22, 0x23, 0x20); then 5 x 4608 sample bytes (block i = ADC command i, read from the raw trigger address, so sample k sits at ring address (raw + k) mod 4096).
- **tools/acqprobe_analyze.py** (stdlib only): `acqprobe_analyze.py <run-dir | ringdump*.bin ...> [--csv out.csv]`. It pairs ringdump_X.bin with acqprobe_X.txt (name prefix swap); run directories hold renamed pairs.
  It checks the magic and length, parses the txt with int-only label=value, and carries its own TIME_DIV_TEXTS and SAMPLE_RATES tables (needs updating if the firmware tables change).
  Output: settings, rates (captures/s, ms per capture, readout us/sample, raw MB/s), raw 0x14 stats, per-block min/max/mean/sd/unique with sparkline, re-read determinism, CSV export (columns k, blkN_0xCC).
  Only when fw_fpga == 1: ring-wrap test (W = 512, 1024, 2048, 4095, 4096, 4097), stale-first-byte check (mismatch at sample 0 only), 32-sample-bin seam scan, capture-fill geometry (folds the highest-sd block into a 4096 ring, finds the longest flat run as the unwritten gap, reports pre/post-trigger depth).
  Non-stock dumps get statistics and CSV only.
- Run data convention: bench/acqprobe/<date-conditions>/ (untracked). Two runs were analysed on 2026-07-19 (open inputs; finger-driven signal).

---------------------------------------------------------------------------------------------------------------------------
## 5. Findings F1..F38 (PORT_AUDIT.md) and applicability to the 1013D variant

Legend: "1013D" = affects or benefits the PORT_1014D 0 build. "N/A" = 1014D UI or hardware only. Nothing below has been verified on a 1013D.

- F1 sm_set_time_base lacked Atlan4's long/short choreography (root cause of the broken timebase keys): N/A (1014D sm_1014d.c; the 1013D uses scope_set_timebase). Fixed.
- F2 strcpy return semantics: RETRACTED. The linked strcpy is a C function in scope_functions.c returning a pointer AT the terminator (non-ISO). strcpy.s is never compiled. `-fno-builtin-strcpy` is required. Applies to both variants.
- F3 dead `#ifndef PORT_1014D` guards gutted touch, battery and RTC on the 1013D build: **1013D, critical.** Fixed to `#if !PORT_1014D` (35 sites), verified only by nm symbol sizes.
- F4 1013D `make` exited 1 at the final cp (same file): **1013D.** Fixed.
- F5 NAV_LEFT/RIGHT key tables disagree: N/A.
- F6 tagThumbnailData gained an unguarded `tracedisplaymode` field mid-struct: **both.** Waveform/thumbnail files are incompatible with pristine Atlan4 firmware. Left as is.
- F7 implicit declarations (sd_card_write include, menu.c cdc_class.h): both. Fixed.
- F8 scope_calculate_sample_range_properties made long_mode aware: shared. Fixed.
- F9 build drift (-O3 to -O2, -fcommon): info.
- F10 1014D fonts, F11 measurement-slot defaults: UI part N/A. F11 also bumped SETTING_SECTOR_VERSION_LOW 0x0015 to 0x0016 (first boot reloads defaults); check whether that is variant-guarded before reusing on a 1013D.
- F12 sidebar measurements never updated, F13/F17/F18 trace-window geometry against the P14 sidebar, F14 probe-magnification semantics, F15 top-bar overflow: N/A (1014D UI).
- F16 normal/single trigger deadlock when the level leaves the signal (level never re-pushed): N/A. The 1013D touch flow pushes the level while dragging.
- F19 Base-calibration reverted on every power cycle (loader gated on the input-cal sector checksum): fixed in shared code by splitting the restore into two blocks (plausibility check: 12 offsets in 100..1600, 4 comps |x| <= 100). **1013D affected**; calibration now also survives Restore defaults.
- F20 manual interleave Trim CH1/CH2: N/A. F21 cal-vs-run mismatch and roll/sweep overlap: N/A (1014D).
- F22/F23/F24 sawtooth elimination sweep (cal rate, cadence, EMI during capture): dead ends; the F24 blocking-acquire code was reverted. Method lesson only.
- **F25 missing FPGA 0x28/0x29 mode select caused the interleave sawtooth at <= 200 ns/div (SOLVED, bench-confirmed on a 1014D).** Restored under `#if PORT_1014D` and `fw_FPGA == 1` only.
  **The 1013D build does not send them** (Atlan4 behavior), although pecostm32's 1013D upstream sends 0x28/0x00 every conversion. A real 1013D with the stock FPGA will likely show the sawtooth. The docs name this as the first candidate fix on a 1013D.
  The stock roll sequence (0x0D <- 0x7D0 and 0x28 <- 1 before every 0x24/0x26 read cycle) is also 1014D only.
- F26 restore pecostm32's second ready-flag read: unconditional, so it **applies to the 1013D.**
- F27 post-F25 noise peaks were an environmental EMI heisenbug (349 kHz SMPS signature); F24 and F26 were non-causal. Lesson: do not credit a fix for a non-deterministic symptom. The pivot to a pecostm32 base was rejected because the acquisition is code-identical to his.
- F28 fabricated measurement precision and integer average discarding oversampling: display part N/A; `CHANNELSETTINGS.averagesum` (mid-struct) is shared and shifts REF-file layout (accepted).
- F29 disabled channel shows garbage (-22 V): N/A (1014D renderers dash it). Stale-value root cause open under F30.
- F30 **OPEN**: Base calibration leaves a non-repeatable +3 to +10 mV DC residual on a shorted input. Calibration accepts an integer ADC1 mean of 125..131 and never verifies the final compensated zero. Integer truncation alone can leave +0.78 count.
  PECOS (pecostm32's firmware) reads +70 mV Vavg at 500 mV/div too. Shared calibration code, so likely **both.**
- F31 AC/DC coupling toggle never reached the FPGA (pecostm32's 1014D has the same flaw): N/A. The 1013D touch flow pushes it immediately.
- F32 cycle/T+/T- readouts 16x low (Q16 producer vs Q20 consumer): N/A (1014D GUI). The shared producer is Q16, consistent with Atlan4's 1013D display path.
- F33 frequency ignored the Si5351 overclock: shared, an exact no-op at scale 1.0.
- F34 sidebar value/unit spacing, F35 offset jump after GEN then MENU (unreproduced): N/A.
- F36 clock-search status residue, F37 roll sweep timing and touch chrome, F38 roll trace retention: N/A (1014D roll renderer; "the 1013D roll path is unchanged").
- 2026-09-20 R1-R10: R1 SD sector move to 2046/2047 is 1014D only (1013D layout unchanged); R4 move-speed contract restored on the 1013D (**1013D**); the rest N/A.
- 2026-08-21 review fixes in shared code (**apply to the 1013D**): CDC `f` clamp, config-restore totalsamples clamp, UINT32_SAMPLE_BUFFER_SIZE round-up, X-Y negative-index guard,
  MSC hardening (no stale data after a failed read, wrap-free range guard, PREVENT_MEDIUM_REMOVAL), I-cache invalidate before the FEL jump, Base-cal 0x0E restore.
- Known and unfixed in shared code (REVIEW): WRITE_10 drops one 512 B packet per staging flush (large writes corrupt; inherited from Atlan4 and pecostm32); READ_FORMAT_CAPACITY malformed reply;
  compute_all_math_ops int8 mul/div sign errors; display_character(int8) sign-extends codes >= 128; uart1_receive_data blocks forever; on fw_FPGA == 1 the 0x0E value is not refreshed per arm (pecostm32 does it), so Base cal on the 1013D build leaves 0x0E stale.
- Infrastructure facts (shared): the D-cache is not actually on (MMU never enabled, so all data accesses are uncached; I-cache works). Never re-enable IRQs inside a handler (start.s resets the IRQ stack pointer each entry).
  USB mass-storage file I/O runs entirely in IRQ context with IRQs masked, so never add SD/FatFs work reachable while the USB screen is up. Six mode stacks are hard-wired in start.s (top 300 KB of DRAM, SVC stack 50 KB) with no overflow detection.
  `types.h` int8 is now `signed char` (Atlan4's `typedef char int8` is unsigned on ARM and caused the menu wrap bug).
- Display facts: two 800x480 RGB565 framebuffers (768,000 B each), maindisplaybuffer (visible) and displaybuffertmp, drawn offscreen then blitted. Trace copy rect (2,48,728,432) on the 1013D, (2,48,705,432) on the 1014D.

---------------------------------------------------------------------------------------------------------------------------
## 6. ROADMAP items and hardware-mod ideas

### ROADMAP.md (34 items; struck items are done)
- Now: 1 hardware-verify the fix passes; 1b sawtooth (done, F25); 2 boot-switch fate (done, Factory settings menu: Restore defaults / Reboot / FEL / Sampling clock / Acquisition probe).
- Refactors: 3 finish the file-level UI swap (conditional object lists); 4 non-blocking key input (UART RX interrupt); 5 warning cleanup then -Werror (234 -Wpointer-sign); 6 replace NetBeans makefiles; 7 dead-weight removal (strcpy.s, compile.sh, test.c);
  8 reference trees as submodules or fetch-refs.sh; 9 CI building both variants (plus froloffw7's QEMU SD-card emulation fork as a smoke-test idea).
- Features: 10 key coverage; 11 calibration track (largely landed); 12 roll-mode UX (rebuilt); **13 PC interface on the 1014D: wire a key or menu to toggle MSC and CDC (USB_CH340); long-term shape a sigrok-compatible data-logger/streaming protocol**;
  14 signal generator decision (the stock 1014D AWG works only under stock firmware; pecostm32 has no generator code; RE by Ghidra of the stock binary or a logic-analyzer capture of Port E; Atlan4's 0x50-0x52 are replacement-FPGA only);
  15 Si5351 as a feature (alternate ADC clocks, CLK2 cal output); 16-18 FPGA and bootloader migration (vendor sources done, retarget done, loader done; open: exercise the fw_FPGA == 2 path on hardware).
- Measurement: 20/21 honest precision and oversampled average (Vavg done, Vrms open); 22 zero a shorted channel in Base cal (F30); 23 guard disabled-channel display (done); 24 push channel enable to the FPGA at runtime (unverified).
- Acquisition ideas (all run on the stock FPGA): 25 multi-capture noise stacking and persistence (keep N records in the ~25 MB free DRAM, align on disp_trigger_index, show average and min/max envelope);
  26 random-interleaved-sampling (RIS) fine-grid overlay (trigger quantization gives a free random sub-sample phase; align by cross-correlation; bin into a 2-8x finer grid; a single-buffer period-fold variant);
  27 burst-capture infrastructure and ring-dump probe (probe done); 28 clock experiments (below-stock presets, fractional MSNA detune helper, boxcar HiRes single-shot);
  29 serial-bus capture aids (n-th-edge anchor, window offset via 0x1F, software hold-off, segmented bursts; about 2500 post-trigger samples cap, Atlan4's 0x0B pretrigger gives mostly-post records);
  30 hardware mods queue; 31 enable the MMU and a real D-cache (largest single performance win; flat section map, DRAM write-back, framebuffer write-through or uncached, 0x01Cxxxxx/0x01Exxxxx device uncached);
  32 roll wait loop should poll keys; 33 MSC protocol repairs (designed, not applied); 34 test.c dead-code sweep.

### Hardware-mod ideas and reasoning (Donwulff notes.md, ROADMAP 30; bench on a 1014D, 2022-23)
- Reality check: each channel has one dual 100 MSPS ADC (AD9288-100, or the Chinese clone MXT2088, 475 MHz analog bandwidth), giving 200 MSa/s per channel, not 1 GSa/s. Sampling performance is about that of a 40 MHz scope.
  The front end has a low-pass corner of about 30-35 MHz, and stock firmware switches to sine estimation above about 44 MHz. A forum poster notes the anti-alias corner should be 40-50 % of Nyquist, so raising it trades aliased noise for bandwidth, which makes stacking and RIS more valuable.
- Bandwidth: replace the buffer opamp RS8751XF (Runic, 180 V/us, 8 nV/rtHz) with OPA356 (360 V/us, 5.8 nV/rtHz); forum user "marauder" did this successfully. Put a KAQY214S solid-state relay (or about 20 ohm) in parallel with R62/R80 to lift the input low-pass corner toward 100 MHz
  (unverified Russian-forum simulation, high-voltage side of the front end). The opamp's main job is buffering the ADC.
- Sensitivity: only 100 mV/div is a real hardware range at 1x probe; 50 mV/div is software zoom (about 7 bit). Relay sequence follows 5 V, 2.5 V, 1 V, 500 mV, 200 mV, 100 mV per div. A higher-gain buffer or real gain stage could make 50 mV a hardware range, but the firmware clamps at range 5 and must be taught.
- Noise: Evi's dual-supply / ground-level opamp mod (fixes ground shift when the front USB is connected to a PC; biases the ADC negative reference); heatsink the ADCs (AD9288 SINAD falls with temperature; Hantek 6022BL ships the same ADC with a glued heatsink;
  bench: ADCs run about 20 C over ambient, the MS5351M about 30 C over, the hottest part); refit the EMI shields (the "breathing" interleave residual; shields were off during tests); power from a battery bank to remove conducted mains EMI (349 kHz SMPS signature).
- Overclock evidence: the 2023 Si5351 "75 MHz" run was really 66.7 MHz (about 267 MSa/s) and looked fine; 80 MHz (320 MSa/s) flipped out. ADCs run over their 100 MSPS rating and the AL3 fabric is near its limit. Treat more than 200 MSa/s as diagnostics.
- Other pointers: Bus Pirate plus flashrom for the ZB25VQ80; FEL with DRAM setup (pecostm32 Test code); firmware backup tool (Test code/fnirsi_1014d_firmware_backup; custom fields live in SPI flash); HAL (emsec/hal) for netlist RE; sigrok data-logger; equivalent-time sampling;
  related open scopes (ataradov/open-5012h, ScopeFun, Earth People DSO 100M, Hantek DSO2x1x Linux).

---------------------------------------------------------------------------------------------------------------------------
## 7. Related repos

### 7a. Donwulff/FNIRSI-1013D-1014D-Hack (D:\src\FNIRSI-1013D-1014D-Hack-donwulff)
- A fork of pecostm32's repo (Schematics, "FPGA explained/", "Software reverse engineering/" with Ghidra output, Original firmware backup, Linux buildroot, sunxi_stuff, Test code, IC's, Manuals, Binaries). Donwulff adds only `notes.md` (17.8 KB, started 2022-12-29).
- notes.md content is folded into sections 2 and 6: bandwidth and Nyquist analysis, vertical sensitivity and opamp, noise and temperature, FPGA depth claims, ZB25VQ80 flash and Bus Pirate, the AL3_10 bitstream header parse, FEL, MS5351M, UART divisor.
- It is about the author's 1014D. It contains little 1013D-specific content beyond the shared spec analysis and the FPGA-family caveat.
- pecostm32's `FPGA explained/` (per-timebase logic-analyzer read sequences, 25nS to 50S/div, plus parameter_analysis.txt for the special IC) is the ground truth for the stock protocol. Other agents are covering that directory.

### 7b. pecostm32/Anlogic_AL3-10_Analyzing (D:\src\Anlogic_AL3-10_Analyzing, 236 MB, mostly generated frames/*.h)
Goal: reverse engineer Anlogic AL3-10 (AL3A10LG144C7) bitstreams from the 1013D and 1014D into Verilog.
Timeline from its README:
- 2022-08: net lists of both FPGAs including pin names.
- 2022-09-25: gate-level Verilog for simple designs.
- 2022-10-15: complete gate-level Verilog for the 1013D; recompiled, it did not work.
- 2022-11-05: MCU communication, relays and brightness working; the reset bit did not match the original.
- 2022-11-10: "finally succeeded in reverse engineering the bit stream into working verilog" (two missed reset-level bits, mslice0 and lslice2).
- 2022-11-19: the readable version fails on the scope because of 200 MHz timing; the crude generated gate-level version works "to some extent" with sample glitches. Project concluded.

Achieved: flash-to-bit conversion; decoded database; bit/frame mapping; netlist tracing and net naming; gate-level and higher-level Verilog; pinout recovery (`Original_1014D_fpga_generated.adc` is tool output); a hand-written functional model of the stock 1013D FPGA.
Unresolved: tile interconnect and global clock naming; bit-exact recompilation (TD version, constraints, place and route).

Tools (all C with NetBeans makefiles; input file names are compiled in via a FILENAME macro, so adapt the source and rebuild; they use the old prjtang x/y convention, not updated to prjtang's new frame/bit one):
- `Anlogic_database_converter`: prjtang JSON database (json_database/al3) to C structures (fixes a swapped X/Y in prjtang bit descriptions; uses cJSON).
- `Anlogic_database_lister_sorter` and `Anlogic_bits_in_tile_lister`: spreadsheet lists and the frame/bit mapping array.
- `Anlogic_bitstream_analyzer`: bitmap, full configuration-bit list, IO pin bits, routing bits (frames/ holds 1000+ generated frame_N_bits.h).
- `Anlogic_netlist_tracer`: reduced copy of the analyzer. Reads `<FILENAME>.bit`; writes `<FILENAME>_gate_level.v`, `<FILENAME>.v`, `<FILENAME>_generated.adc`, and block/net/setup CSV lists. Handles mslice/lslice LUT2-6, registers, PLL, EMB (BRAM) macros. pin_assignments/ holds package data.
- `Anlogic_flash_to_bit_converter`: SPI flash image to TD ASCII `.bit`. The flash starts with 32 bytes of 0xFF split into two blocks with big-endian 16-bit sizes, then the preamble 0xCC55AA33, then tagged records (tag 1 B, flag 1 B, size 2 B, data, CRC-16 poly 0x8005);
  a frame is 257 data bytes plus CRC plus 4 zero pad bytes. It writes the TD ASCII header ("Version: 5.0.28716, Architecture: al3_10, Package: LQFP144") whose "Bitstream CRC" must be computed (single-file, hard-coded names).
- `pythonProject/`: prjtang-derived Python to decode TD IDE work files and the database (decode_file.py, decrypt_all.py, create_database.py, unlogic.py). `decoded_database/al3_10_database_decoded` adds pad-to-package-pin and routing data missing from the JSON.

Data:
- `FNIRSI-1014D_FPGA/`: flash_fpga_1014d.bin (1 MB), Original_1014D_fpga.bit, .v (349 KB), _gate_level.v, block/net CSVs, `Original_1014D_fpga_generated.adc` (authoritative 1014D pinout).
- **`FNIRSI-1013D_FPGA/` (directly relevant, not mentioned in the Donwulff docs): Original_1013D_fpga.bit (285,317 B), Original_1013D_fpga.v (411 KB machine-decompiled), `finrsi_1013D/fnirsi_1013d.v` (hand-cleaned, with ModelSim testbench and .do scripts),
  and `Original_1013D/`, a complete buildable TD 5.0.28716 project (Original_1013D.al, source/Original_1013d.v, Original_1013D.adc, Original_1013D.sdc, al_ip/pll + sample_memory) that re-implements the STOCK 0x1432 1013D FPGA.**
  It is a functional model, not bit-exact ("not an exact match to the original but it seems to work"). The I2C special-IC part is not implemented.
- What that model documents: the full 1013D AL3 pinout (section 2); PLL 50 to 200 MHz; 50 MHz peripheral clock; 1 kHz calibration output; offset PWMs (period 2000 clocks at 50 MHz = 25 kHz); backlight PWM (period 60000 = 833 Hz);
  sample_clock_enable = divider < counter + 1; alternating encA/encB for the two ADCs per channel; 12-bit write/read addresses; trigger checking starts after address > 1600; trigger_read_address preloaded to 800; the complete stock command set;
  trigger by comparing the current sample with the inverted previous sample; the reset-state flag asserts 7 clocks after reset; version bytes 0x14 0x32. It has no 0x28/0x29 handling.
- Repo license: see D:\src\Anlogic_AL3-10_Analyzing\LICENSE (not read).

---------------------------------------------------------------------------------------------------------------------------
## 8. Assessment

### Is this repo the best base for new 1013D work?
No as a code base, yes as a knowledge base. The 1013D variant is the least validated half of this tree.
- About 95 % of the shared code is Atlan4 v1.00o5, the 1013D code that evolved on real 1013D hardware, including CDC, generator, roll mode and touch UI. The 1013D variant here is "Atlan4 plus dead-guard fixes plus a few shared-core fixes".
- What it adds over a pristine Atlan4 zip: F3/F4 buildability fixes, the F19 calibration-persistence split, the F26 ready double-read, DoS-type clamps (CDC `f`, config restore, buffer round-up, X-Y OOB), MSC hardening, the FEL I-cache fix, GCC 14 headless buildability,
  a host regression harness for both variants, SD-layout and loader checks, and extensive documentation.
- What it costs: about 14k lines of 1014D UI and state machine and a dual-variant `#if` mesh. 1014D fixes were deliberately not ported to the 1013D (F25 0x28/0x29, roll rebuild), so a real 1013D with the stock FPGA likely regresses to Atlan4's behavior there.
- Versus Atlan4's zips (github.com/Atlan4/Fnirsi1013D): Atlan4's is the field-proven 1013D build and ships the FPGA sources (AL3, EF2, Altera) and bootloaders, but with no tests or docs. Treat the 1013D target here and Atlan4 v1.00o5 as the same functional code; take the fixes listed above from this repo.
- Versus pecostm32's trees: FNIRSI_1013D_Firmware is smaller and cleaner (blocking acquisition, 0x28/0x00 every conversion) but has no CDC, generator or roll. His Hack repo and Anlogic_AL3-10_Analyzing hold the authoritative stock FPGA protocol and a working 1013D FPGA model, which this repo does not vendor.
- Recommendation: start 1013D work from this repo's `PORT_1014D 0` target (or Atlan4 v1.00o5 directly; the code is shared), keep the test harness and layout checks, and treat everything on the 1013D path as unverified until bench-tested.
  First action on real hardware: FEL-load (RAM only) the 1013D build and check touch, battery, RTC, acquisition and the sawtooth at <= 200 ns/div; if present, port the F25 0x28/0x29 writes to the 1013D path.
- Take regardless: acqprobe plus fpga_dump_ring plus the analyzer, the capture-geometry numbers, tools/tests mock harness, Makefile and SD-layout checks, boot-contract notes, and the Original_1013D Verilog from Anlogic_AL3-10_Analyzing.
- Do not inherit: the 1014D UI layer and the long agent-written narrative (many stale statements, as its own review admits).

### State of the 1013D build
- Compiles (author reports a clean 1013D build, 257 warnings baseline; 1014D 266). Never run on a 1013D from this tree.
- Touch (GT911 on PA0-3), DS3231 RTC on PA2/PA3, battery, the Atlan4 loader v0.8 chain and the sector-710 display config are inherited from Atlan4 and unverified here.
- Gaps on a stock-FPGA 1013D: no 0x28/0x29, no stock roll sequence, 0x0E not refreshed per arm; generator code sends 0x50-0x52 (no effect on stock, and the 1013D has no AWG); CDC is a debug stub with 11 variables.
- With an Atlan4 replacement FPGA (fw 2/3): 0x0B/0x0C geometry and deeper settable capture are Atlan4's own path, and the viable way to deeper memory (zaklad.v has 8192 x 32 bit, 13-bit addresses).
  Bitstreams are board-pinned, so any bitstream must match the user's exact FPGA family (AL3 vs EF2 vs Altera).

### Risks and unknowns to carry forward
1. The user's FPGA family (AL3A10LG144C7 vs EF2L45LG144B) decides toolchain, pinout and programming route. The AL3 pinout is Original_1013D.adc; the EF2 pin map is not in these repos (it is in Atlan4's EF2 project).
2. The TD community license expired on 2026-08-31 per the notes; a fresh .lic is needed. No build-time figures are recorded.
3. No ARM toolchain on this box; the repo's tests were not run.
4. Much of the F-series is agent-written narrative from bench logs; some statements were later corrected (see REVIEW). Cross-check against code.
5. The CDC parser runs in IRQ context and TX flushes block; do not extend it as is for sample streaming.
