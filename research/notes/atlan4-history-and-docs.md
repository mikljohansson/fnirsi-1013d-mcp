# Atlan4 / Fnirsi1013D: version history and documentation digest

Sources (untrusted data, read only, nothing executed):
- Repo `D:\src\Fnirsi1013D` (github.com/Atlan4/Fnirsi1013D). Git has ONE commit: `b0397d6` "Update readme", author Atlan4, date header "Sun Jun 21 17:58:01 2026 +0200". No history; the real history lives in zip and folder names.
- Extracted zips: `D:\src\fnirsi-1013d-ai\research\atlan4-extracted\<zip>\<zip>\...` (90 folders) and `...\_dso1xxx\<name>\...` (fw 1.00g..1.00o5).
- The .doc manual was extracted with `antiword -m UTF-8` (Word COM not registered, python olefile not installed). Text only; the screenshots (`[pic]`) are not recoverable that way (see Part 2).
- Upstream base: pecostm32/FNIRSI_1013D_Firmware (root README.md says so). Atlan4's variant is the "PECO" firmware: alternative firmware that runs from the SD card.

## 0. Dating anchors (no changelog file exists in the repo)
- `build_time.h` (COMPILE_YEAR/MONTH/DAY/HOUR/MIN/SEC) exists from 0.26y5 on:
  - v0.26y5 25-10-31 18:07; v0.27i 25-11-01 10:08; v0.27j 25-11-01 11:43; v0.27m 25-11-04 15:18; v0.27p 25-11-05 15:34; v0.27r 25-11-05 21:03; v0.27s 25-11-06 08:52; v0.27t 25-11-10 09:12
  - v0.28a 25-11-19 15:30 (branch off 0.27t; has NO generator.c); v1.00g 25-11-24 14:34; v1.00h 25-11-24 18:39; v1.00i 25-11-24 21:01; v1.00j 25-11-25 13:45; v1.00lv1 25-11-26 12:36; v1.00l3 25-11-26 22:13; v1.00l5 25-11-27 09:36; v1.00o5 26-02-13 18:59
- Source-comment dates: `PC_interface.c` "Author: Atlan, Created November 2, 2025"; `generator.c` "Author: dk, Created November 26, 2025"; `fpga_control.c` comment "change 11.12.2025" (older line says 11.12.2023); `menu.c` comments "21.11.2025".
- `VERSION_STRING` in `variables.h` doubles as inline changelog, e.g. v0.28a: `"v0.28a" // sample to 8000, data triger to 8192 mod kurzor_position`; v1.00o5: `"v1.00o5" //fix EF2 circle buffer to 6144, add text in diagnostic screen`. `scope_prepare_setup_for_file()` copies the first 7 chars of VERSION_STRING into each saved waveform file (byte offset 8).
- Other version numbers: manual says "applicable version is v0.027u"; doc filename says v1.00n3v ANG v1. `Guide to firmware/firmware_Atlan_Peco/` ships bootloader v0.7, updater v0.02r, scope v0.26y2, scope v0.27u. Transition folder: `fnirsi_1013d_migration.bin` = bootloader v0.06 + updater v0.02g + scope v0.26v9; `fnirsi_1013d_migration v0.2.bin` = bootloader v0.7 + updater v0.02l + scope v0.26y2. FPGA readmes: "install bootloader v0.8 or higher" for 1.xxx. Loader exe v1.1.3 (loader_v1.1.3 changelog in Part 2).
- Extraction problems: zips `v0.020e to v0.021c` and `v0.26v4 on ref if load or copy, add button load ref to start up` extracted EMPTY (no content). `FPGA.zip` -> `FPGA/FPGA/` (34 files, not analysed here).
- Several files named `*.txt` are BINARY firmware (start with `eGON.BT0`, Allwinner boot header): `fnirsi_1013d.bin v0.025c.txt`, `latest firmware version/*.txt` (v0.026j, v0.26k4), `Scope*/dist/.../publikovane/*.txt` (v0.26q10, v0.26vB, v0.26v6/v7/v9, v0.26s1, fnirsi_1013d-ALL.bin.txt), `Firmware for restore backup/FNIRSI_1013D_main.bin.txt`, `fnirsi_1013d_fwr.bin.txt`. They are forum-upload renames, not documentation.

## 1. Timeline

### 1.1 Phases (from folder names, VERSION_STRING, file-set diffs)
1. **pecostm32 base** (`versions.txt` inside the bootloader zips): v0.002 initial release of the new firmware; v0.003 measurements added; v0.004 (08 Mar 2022) swap of touch X and/or Y added to the display configuration file; v0.005 (09 Mar 2022) autosetup switches back to AUTO (froze in NORMAL/SINGLE) + full-screen init in NORMAL/SINGLE; v0.006 (12 Jan 2023) better RMS by Michal Derkacz (ziutek). Snapshots in `zaloha/` keep "V0.006". Atlan4 "V0.025d/V0.025m" appear as VERSION_STRING in the same tree.
2. **v0.012..v0.013f** (`v012- v0.013f.zip`, Atlan4 start): v0.013b colored START/stop button; v0.013c clear + select-all buttons in measurement menu; v0.013f measurement-menu size. Also folders `v1.012 thumbnail color curzor trigger`, `v1.012a - correction of selection of non-existent thumbnail items`, `v1.012b - correction of assembly lines after the delete menu`, `v1.013a Trigger range correction and center tuning` (VERSION_STRING "V1.012/V1.013/V1.013a" = typo for 0.012/0.013; ordered before 0.013l here). These four also contain the 3-stage SD loader trees `fnirsi_1013d_sd_card_bootloader`, `fnirsi_1013d_startup_from_sd_card`, `fnirsi_1013d_startup_screen` (pecostm32 design: SD bootloader -> startup screen waits on FPGA -> scope loaded to 0x80000000; see his todo).
3. **v0.013l..v0.016a** (AVG ch1 copied to ch2; simpler trigger; high-frequency trigger update; axis move disabled if channel off; RTC): v0.014f first `DS3231.c/.h` ("pokusy s trigerom a priprava RTC"); v0.015a first RTC; v0.015g switch to FatFs RTC timestamps (diskio.c); v0.015h FAT date support; v0.015i "rtc ready 99%"; v0.016 RTC off; v0.016a RTC on.
4. **v0.016b..v0.019b**: wave-view next/prev button fix; v0.016d RTC menu; v0.016f picture menu ok; v0.016h month day 30/31 fix, run-stop in single mode, WAIT text; v0.017c/d date+time in picture view and waveform view; v0.018c DC-mode signal shift; v0.018g first auto-DC; v0.018k V1/V2 cursor colors, y1/y2/dY; v0.019a sensitivity in channel menu; v0.019b "fix long connect to win" (mass_storage_class.c).
5. **v0.019g..v0.022 (calibration + menu system)**: v0.019g fix LCD port setting (display_control.c); **v0.020 adds Menu.c/.h** (4567 changed lines); v0.020a calibrating text; v0.021d..f calibration (fail=0, offset-failed text, "final calibration"); v0.021g main menu dimensions; v0.021h input calibration into menu; v0.021j fix FPGA offset for all channels; v0.022 Auto-AC accepts offset.
6. **v0.022b..v0.023x (autoset + long time base)**: v0.022b-g autosetup rewrite (v0.022d "frist release autosetup", v0.022f autoset sample frequency); v0.022h long time base + max-light button; v0.022k adds test.c/.h and renames Menu.c to menu.c; v0.022m "text reset default value, need calibration"; v0.022p battery percentage; v0.022q-v0.023m long time base (roll/scroll) incl. trigger ("v0.023m long mode OK"); **v0.023d settings moved to sector 709**; v0.023x time base number change.
7. **TP write v0.023x** (`TP write config v0.023x.zip`: `0.023 TP1..TP4 write`, VERSION_STRING "V0-wrTP"): helper firmware that writes touch-panel config.
8. **v0.024..v0.024y (DC offset, average, sector 708)**: v0.024 diag menu; v0.024a temperature on diag screen; v0.024b/c DC offset ch1/ch2; v0.024d DC-shift correction factor; v0.024e fixed shifted signal at 50 mV with two channels; v0.024f ADC compensation in long mode; v0.024h FPGA average read + Average mode + A/V (ampere) mode; v0.024l-p more probe dividers (0.5-1-10-20-50-100-1000), channel menu divider; v0.024p fix calibration switch 0.5x -> 1x; **v0.024q input calibration data moved to sector 708**; v0.024r trigger horizontal position stored as 2x16 bit; v0.024s middle ACQ menu; v0.024v old and new autosetup; v0.024x shift calibration button; v0.024y long memory mode.
9. **v0.025a..v0.026a (DC-shift menu, new bootloader, icons, CH340)**: v0.025a DC-shift calibration menu V+/V-; v0.025c/d "with new bootloader" (adds `FNIRSI_1014D_Firmware-main/fnirsi_1014d_scope`, VERSION "V A0.002"); v0.025f DC-shift menu with autoset, boot fw0.03; v0.025l boot menu for bootloader fw0.04; v0.025n removed FPGA commands 0x28 and 0x3C, trigger level in long mode, ACQ long/short switch fix; v0.025o remote sample freq and time prediv, single trigger fix; v0.025q diag menu moved to main menu + icons menu + battery indicator; v0.025s **lock move cursors**; v0.025u fast switch backlight to max (v0.025v saves it); v0.025x roll mode + overload red-line fix; v0.025y1 frequency calculation changed in FPGA code and **adds kiss_fft (`_kiss_fft_guts.h`, `kiss_fft*.c`), `ch340_class.c/.h`, `commands.c/.h`**; v0.026a long memory OFF.
10. **v0.026b..v0.026j (UI rework)**: v0.026b default button + "fw0.05 bigger button" (zip carries the full pecostm32 tree `FNIRSI_1013D_Firmware-main`); v0.026c TP button change + **keyboard** (`scope_open_keyboard_menu`, `keyboard_texts[]`); v0.026e top cursor move, 50 mV error fix; v0.026f invert variable; v0.026h channel menu + **invert button**; v0.026i new autosetup works in invert mode; v0.026j rounded-rect menu items.
11. **v0.26k..v0.26v5 (menu redesign, reference waveforms, math)** (folder naming switches from 0.026x to 0.26x): 0.26k2/k3/k4 ACQ + brightness icon moves, makefile for FEL; 0.26k5 trigger menu; 0.26l trigger button/menu moved; **0.26m ref1 & ref2**; 0.26n/o trigger menu graphics; 0.26p channel sensitivity menu; **0.26p1 REF and MATH menu**; 0.26p2 define all menus; 0.26p3 display copy for ref & math; 0.26p4-p6 "other" menu + settings; 0.26p7 channel-menu touch problem; 0.26p11 if/else solved; 0.26q/q1 icons done; 0.26q2 X-Y switch + notification done; **0.26q3 generator button added**; 0.26q4 beta test; 0.26q6 ref1-ref4 ok; **0.26q7 sum and diff (math)**; **0.26r2 BMP time stamp** (adds `ref_and_math.c/.h`); 0.26r5 save ref ok; 0.26r6 save + load ref; 0.26s ref1_4 renamed channel ref1_4; 0.26s2 two buttons in other menu + save-index/wave-index fix; 0.26s3 clear-RTC button waits for touch; 0.26u6 menu reworked to **ref8**; 0.26u8 beta; **0.26u9 ref auto-load on start** ("Load REF to start") + save/load ref fix; 0.26v2 touch-move ref1-ref8; 0.26v5 faster cursor movement ("often check TP").
12. **Updater / bootloader / scope split** (zips `updater 0.01b .. 0.02o`; updater trees' VERSION_STRING v0.01l, v0.02g, v0.02o; embedded scope builds v0.26v8, v0.26v9, v0.26y2): updater 0.01b (scope v0.26v8), 0.01q-0.02c (scope v0.26v9, "add fail text in base calibration procedure"), 0.02j/0.02l (scope v0.26y2, "test generate CSV file with slow fast button"). Updater steps: 0.01f makefile; 0.02c updater startup picture (updater.bmp); 0.02g **upload buttons for SPI flash and FPGA flash blocked unless SPI backup exists**; 0.02m address for FPGA write; 0.02n **verify after SPI write** + error messages on file/SPI write; 0.02o lower SPI speed. `Bootloader fw0.02/0.03/0.04` = pecostm32's `fnirsi_1014d_startup` modified for the 1013D (touch display support added, UART for 1014 removed; root README). fw0.02 -> fw0.03: only 1014d startup (display_control.c/.h, fnirsi_1014d_startup.c); fw0.03 -> fw0.04: 1014d startup .c plus scope-side fnirsi_1013d_scope.c, fpga_control.c, menu.c/.h, scope_functions.c, statemachine.c, test.c, variables.c/.h (boot menu, v0.025l). Boot menu (bootloader >= 0.04): choose PECO / FNIRSI / FEL; default shown with green frame; default starts after 3 s, touch extends by 10 s; FNIRSI and FEL always show the menu (otherwise one could not return).
13. **v0.26y5 "work ch340 ok"** (25-10-31; comment "TP add delay before read tp") and repo folder `Scope 1013D v0.26y4 clear` (same lineage: ch340, no cdc/PC_interface). Repo `dist/` contains `fnirsi_1013d.bin` (SD-card image), `fnirsi_1013d_scope.bin/.elf` (FEL test), `sunxi-fel`, display/TP config sector images (`standard_display_config_sector.bin`, `shifted_display_config_sector.bin`, `standard_display_tp_xy_swap_config_sector.bin`, `mstandard_display_config_sector.bin`), 0-byte files `gnome`, `gnome-session`, `logout-prompt` (stray), `readme`, `How_to_load_scope.txt`.
14. **USB CDC serial / PC app: v0.27i..v0.27t** (25-11-01 to 25-11-10): see 1.3.
15. **v0.28a** (25-11-19, repo folder `Scope v0.28a`): branch from 0.27t: `MAX_SAMPLE_BUFFER_SIZE 16382` (was 3000, `//3000//4095` kept as comment), "sample to 8000, data triger to 8192, mod kurzor_position"; adds `uint16 settriggerpoint; totalsamples` to scopesettings; FPGA cmds 0x1B/0x1C for pretrigger/total; `fpga_write_int(16000)` hard-coded sample-rate divider (old `timebase_settings[timebase]` commented); no generator.
16. **1.00x ("DSO 1.xxx", needs new FPGA bitstreams)** 25-11-24 to 26-02-13: see 1.4.

### 1.2 Key milestones
| item | first seen | note |
|---|---|---|
| DS3231.c/.h (RTC) | 0.014f | optional hardware mod, disabled by default |
| Menu.c (menu.c from 0.022k) | 0.020 | |
| test.c/.h | 0.022k | |
| SETTINGS_SECTOR | 700 until 0.023c; 709 from 0.023d | |
| INPUT_CALIBRATION_SECTOR | 708 from 0.024q | DC_SHIFT at byte offset 40 |
| DISPLAY_CONFIG_SECTOR | 710 | LCD/TP/boot config |
| WAVEFORM_FILE_VERSION | 0x01000002 in all Atlan4 snapshots (252 hits); 0x00000001 only in 10 older pecostm32-based trees | VIEW_NUMBER_OF_SETTINGS = 200 words |
| kiss_fft files | 0.025y1 | manual still says "FFT is not yet available" |
| ch340_class.c | 0.025y1 | |
| ref_and_math.c/.h | 0.26r2 | |
| cdc_class.c, PC_interface.c/.h | 0.27m | |
| generator.c/.h | 1.00l3 | in l3, l5, lv1, o5; NOT in v0.28a or 1.00g-j |
| `fw_FPGA` detection | 1.00o5 only | verified: 0 hits in 1.00g..lv1 |
| generator FPGA cmds 0x50/51/52 in fpga_control.c | 1.00g | |

### 1.3 USB CDC work v0.27i..v0.27t
- 0.26y5: usb_interface.c (1626 lines) + ch340_class.c (283) + mass_storage_class.c.
- 0.27i (25-11-01) "CDC serial working, switch MSC to CDC": changes ch340_class, mass_storage_class.c/.h, usb_interface.c/.h, menu.c, fnirsi_1013d_scope.c (526 diff lines).
- 0.27j: "displej 2512data", ch340/usb_interface tweaks.
- 0.27m (25-11-04) "usb_cdc_set_line_coding OK": adds `cdc_class.c/.h` and `PC_interface.c/.h`, removes `ch340_class.c/.h`; usb_interface.c handles the 7-byte SET_LINE_CODING (baud, stop bits, parity, data bits); default `dwDTERate = 9600`.
- 0.27p (25-11-05): CDC buffer 512, callback buffer 512.
- 0.27r (25-11-05 21:03) "YES work it with APP" (a PC app can talk to the scope over CDC).
- 0.27s: clear CDC settings; callback moved to the main loop.
- 0.27t (25-11-10): "clear usb interface code" (usb_interface 1846 lines; PC_interface, statemachine, test touched).
- CDC device descriptor (`cdc_class.c`): VID 0x0483, PID 0x5740 (ST virtual COM ids), bcdDevice 0x0310, 2 interfaces, bus-powered, 100 mA.
- `PC_interface.c`: packed `sysparam SysParam` struct mimicking a vendor DSO parameter block (run_state, ch2_enabled, time_zoom 2=5 ns..30=10 s, horiz_position[3], trigger channel/mode/edge/source, trigger_level, chX_volt_zoom 4..13, chX_vert_position, coupling DC/AC, probe 1x/10x/100x, ...); `usb_CDC_send_data`, `usb_CDC_process_rx` (only the command line `#ECHO`; its body is commented out); local string helpers. Rx ring 1024 B, tx ring 32768 B. In 1.00o5 PC_interface.c has 536 lines and still only `#ECHO`.
- Manual option "USB serial mode": after power-on the scope is a serial port for terminal commands (diagnostics/development).

### 1.4 The 1.00x line (_dso1xxx)
- Also stored as `Verzia 1.xxx/DSO 1.xxx/backup dso/*.zip` (same 8 zips). Folder notes (Slovak): 1.00g "add % in brightness and center text, done gen menu"; 1.00h "clear gen menu, and little grafik corection"; 1.00i "statemachine button add for gen menu"; 1.00j "work on keyboard, move menus to Y48, add menu open flags"; 1.00l3 "zadavanie OK, treba doprogramovat obmedzenia napr duty max 100%" (entry works, limits still to code); 1.00l5 "fix error on signal 25 or 75% triger"; 1.00lv1 "zadavanie z klavesnice funguje, treba dokoncit pre freq duty hold off a ostatne" (keyboard entry works, still to finish for freq/duty/hold off/others); 1.00o5 "fix EF2 circle buffer to 6144, add text in diagnostic screen".
- Generator FPGA control (fpga_control.c; the three functions exist since 1.00g): `fpga_on_off_generator()` -> cmd 0x50 (1 output on, 0 tristate; "off" also sets 100 Hz / duty 0); `fpga_set_generator_freq()` cmd 0x51, phase_inc = f * 2^32 / 50,000,000 (50 MHz clock); `fpga_set_generator_duty()` cmd 0x52, threshold = duty% * (2^32-1) / 100. FPGA command table `Verzia 1.xxx/FPGA for 1.xxx/FPGA commands.txt` lists 0x50 (output or tristate), 0x51 (clk-out divider, 4 bytes high first), 0x52 (clk_out_duty, 4 bytes).
- Settings struct fields: gen_enable, gen_wave_select, gen_freq (uint32), gen_duty (uint8), gen_ampl, gen_offset, gen_phase (uint16). Only freq and duty reach the FPGA; ampl/offset/phase values are kept, FPGA setter calls are commented out (manual: hardware supports only SQUARE; amplitude/offset/phase only for an external generator). `GEN_f_MAX 10000000` (code comment says 5 MHz; manual says 0 Hz to 10 MHz). Generator button at (630,5), menu at (508,48) 220x393. Menu quick buttons: freq -> 1000 Hz, duty -> 50 %, "2.50V" offset button.
- **Keyboard entry for generator** (1.00j starts, 1.00l3/lv1/o5 finish): `generator.c` number buffer (11 chars, "_" cursor; buffer_clear/set_sign/add_digit/backspace/to_int32/set_number); keyboard menu 5x4 keys `7 8 9 m k / 4 5 6 u M / 1 2 3 n G / 0 , +/- p T` (SI prefixes m, k, u, M, n, G, p, T); `scope_open_keyboard_menu()`, `handle_keyboard_menu_touch()` in statemachine.c; open-flags freqmenuopen/dutymenuopen/amplmenuopen/offsetmenuopen/holdoffmenuopen. Clamp: freq 0..GEN_f_MAX, duty 0..100. Hold-off also opens the keyboard but its FPGA write is commented out (`//buffer_set_number(fpgasettings.value_holdoff_trigger)`). Same keyboard menu code first appeared in 0.026c.
- FPGA detection in 1.00o5: `fpga_get_version()` (16 bit): 0x1432 -> fw_FPGA=1 (original FNIRSI FPGA firmware: forces total samples 3000, trigger point 1500 = 50 %), 0x1532 -> 2 (PECO fw, AL3-A10), 0x1632 -> 3 (PECO fw, EF2-L45). The diagnostic screen prints "FPGA version", "fw: Fnirsi/PECO", chip name. For fw_FPGA>1: pretrigger via cmd 0x0B, total samples via 0x0C (0.28a used 0x1B/0x1C), sample divider from `scopesettings.samplecount`, then `fpga_set_time_base(timeperdiv)`.
- Samples: MAX_SAMPLE_BUFFER_SIZE 16382 (manual: "With firmware v1.00 and higher maybe 16000 samples"). Memory depth and trigger position 5/50/75 % only with the new FPGA firmware.
- o5 also contains a fixed-point (FP_SHIFT 20) frequency calculation (period = 2 x (high+low)); it sits after a `/*` comment opener in `fpga_control.c` (~line 850), so it is probably disabled; I did not verify where the comment closes.
- FPGA sources for 1.xxx: `FPGA AL3` (Anlogic TD project AL3S10, original `fpga-1013D-1-zb25vq80.bin`, custom `FPGA.bin`, program SPI flash with CH341 + 3.3 V converter), `FPGA EF2` (EF2L45 project, Anlogic TD 4.6.8 SP1 required on Windows, programmer = STM32F103C8T6 usbjtag, original `EF2 fnirsi backup.bin`; README: restoring the original file in EF2 is not yet possible), `FPGA EP4CE06` (Quartus 22.1, `fnirsi_1013D_EP4CE6.bin`, `rbf2bin` tool converts .rbf -> .bin, CH341 flash). All readmes: "install bootloader v0.8 or higher first"; "No one is responsible for anything".
- FPGA commands (read): 0x05 sampling reset state, 0x06 FPGA version (0x14,0x32 in FNIRSI image), 0x0A sampling done, 0x14 trigger address, 0x18 triggered, 0x1F data read offset, 0x20-0x23 sample memory per ADC/channel, 0x24-0x27 live ADC, 0x41 touch panel address. Write: 0x01 reset, 0x0B pretrigger, 0x0C total samples, 0x0D sample-rate divider (32 bit), 0x0E time-base counter, 0x0F start conversion, 0x15 trigger channel, 0x16 edge, 0x17 level, 0x1A mode, 0x1B hold-on divider, 0x32/0x35 ch1/ch2 DC offset, 0x33/0x36 sensitivity relays, 0x34/0x37 AC/DC, 0x38 brightness, 0x3C touch read address, 0x50/0x51/0x52 clk out (generator).

### 1.5 Per-snapshot diff table
Order is by VERSION_STRING; overlapping zips (0.026b..f vs 0.026b..i) are de-duplicated by folder name. Diff lines = lines differing (`<`/`>`) vs the previous row excluding build/dist/nbproject/binaries; `+` added, `-` removed files. The 0.26y4 row compares against v0.28a so it is not meaningful.

| snapshot (folder) | diff lines | changed files |
|---|---|---|
| 0.013b color button START-stop | (base) | |
| 0.013c add butoon clear and select all inmesurents menu | 35 | scope_functions.c, statemachine.c, variables.h |
| 0.013f Editing the size of the measurement menu | 87 | scope_functions.c, statemachine.c, variables.h |
| 0.013l AVG channel1 copy to chanel 2 | 65 | fnirsi_1013d_scope.c, scope_functions.c, variables.c, variables.h |
| 0.013n AVG ch1 copy to ch2 | 43 | scope_functions.c, variables.h |
| 1.012 thumbnail color curzor trigger | 211 | fnirsi_1013d_scope.c, fpga_control.c, power_and_battery.c, scope_functions.c, statemachine.c, variables.c, variables.h |
| 1.012a - correction of selection of non-existent thumbnail items | 17 | scope_functions.c, variables.h |
| 1.012b - correction of assembly lines after the delete menu | 4 | statemachine.c |
| 1.013a Trigger range correction and center tuning | 27 | fpga_control.c, power_and_battery.c, scope_functions.c, variables.h |
| 0.014c another triger simply | 182 | fnirsi_1013d_scope.c, scope_functions.c, statemachine.c, variables.c, variables.h |
| 0.014d update trigger for high f | 39 | scope_functions.c, scope_functions.h, variables.h |
| 0.014e zakazanie posuvania osi ak je dany kanal vypnuty | 6 | statemachine.c, variables.h |
| 0.014f pokusy s trigerom a priprava RTC | 152 | +DS3231.c, +DS3231.h, fnirsi_1013d_scope.c, scope_functions.c, touchpanel.c, touchpanel.h, variables.c, variables.h |
| 0.015 | 422 | DS3231.c, DS3231.h, fnirsi_1013d_scope.c, fnirsi_1013d_scope.h, scope_functions.c, scope_functions.h, statemachine.c, variables.c, variables.h |
| 0.015a frst RTC | 166 | DS3231.c, DS3231.h, fnirsi_1013d_scope.c, fnirsi_1013d_scope.h, scope_functions.c, scope_functions.h, statemachine.c, touchpanel.c, variables.c, variables.h |
| 0.015b oprava tlacidla startstop a autoset | 63 | fnirsi_1013d_scope.c, scope_functions.c, variables.h |
| 0.015c pokus o zmenu rozmestnie nameranych poloziek | 52 | fnirsi_1013d_scope.h, scope_functions.c, variables.c, variables.h |
| 0.015d rozpracovane pridanie time a date do files | 51 | DS3231.c, DS3231.h, scope_functions.c, variables.c, variables.h |
| 0.015e | 64 | DS3231.c, DS3231.h, scope_functions.c, variables.c, variables.h |
| 0.015g prechod na RTC FS fat | 261 | DS3231.c, DS3231.h, diskio.c, fnirsi_1013d_scope.c, scope_functions.c, touchpanel.c, variables.h |
| 0.015h podpora datumu fat | 59 | DS3231.c, variables.h |
| 0.015i rtc ready 99% | 252 | DS3231.c, DS3231.h, fnirsi_1013d_scope.c, scope_functions.c, timer.c, variables.c, variables.h |
| 0.015k oprava posuvania ch1 po autosete | 7 | scope_functions.c, statemachine.c, variables.h |
| 0.016 off RTC | 8 | DS3231.c, fnirsi_1013d_scope.c, variables.c, variables.h |
| 0.016a rtc on | 6 | DS3231.c, variables.c, variables.h |
| 0.016b fix next and prev buton finction on wavwfroms | 56 | scope_functions.c, scope_functions.h, statemachine.c, variables.h |
| 0.016d menu RTC | 294 | icons.c, scope_functions.c, scope_functions.h, statemachine.c, variables.c, variables.h |
| 0.016e change displaybuffer 1.2.3.tmp | 181 | DS3231.c, fnirsi_1013d_scope.c, scope_functions.c, scope_functions.h, statemachine.c, variables.c, variables.h |
| 0.016f picture menu ok | 465 | DS3231.c, DS3231.h, icons.c, scope_functions.c, scope_functions.h, statemachine.c, variables.c, variables.h |
| 0.016g RTC ready 99.9% | 455 | DS3231.c, DS3231.h, display_lib.c, scope_functions.c, scope_functions.h, statemachine.c, variables.c, variables.h |
| 0.016h change day 31 a day 30 for month & fix buton run-stop in single mode add WAIT text | 92 | DS3231.c, DS3231.h, fnirsi_1013d_scope.h, scope_functions.c, statemachine.c, variables.h |
| 0.016i single mode screen mismas | 29 | fnirsi_1013d_scope.c, scope_functions.c, statemachine.c, variables.h |
| 0.017 | 78 | DS3231.c, fpga_control.c, scope_functions.c, scope_functions.h, variables.h |
| 0.017a fix litle grafic | 176 | scope_functions.c, statemachine.c, variables.h |
| 0.017c add pictureview data and time, fix thumbnail name whitout rtc data  | 86 | DS3231.c, DS3231.h, scope_functions.c, scope_functions.h, statemachine.c, variables.h |
| 0.017d Add time a date to waveform view | 12 | scope_functions.c, variables.h |
| 0.017e | 201 | DS3231.c, DS3231.h, fnirsi_1013d_scope.c, scope_functions.c, variables.c, variables.h |
| 0.018a | 27 | fpga_control.c, scope_functions.c, statemachine.c, variables.c, variables.h |
| 0.018c add shift signal in DC mode | 105 | fpga_control.c, scope_functions.c, statemachine.c, statemachine.h, variables.c, variables.h |
| 0.018d | 15 | scope_functions.c, variables.h |
| 0.018e | 102 | fpga_control.c, scope_functions.c, statemachine.c, variables.h |
| 0.018g frst version autoDC and fix ac position on menu | 189 | fpga_control.c, scope_functions.c, scope_functions.h, variables.h |
| 0.018h change color name change limit to 396 | 181 | fnirsi_1013d_scope.c, fnirsi_1013d_scope.h, scope_functions.c, statemachine.c, variables.h |
| 0.018k change color for curzor V1 and V2 add y1 a y2 and dY | 114 | DS3231.c, mass_storage_class.c, scope_functions.c, statemachine.c, variables.h |
| 0.019a add sensitivity in channel menu | 407 | scope_functions.c, scope_functions.h, statemachine.c, variables.c, variables.h |
| 0.019b fix long connect to win | 13 | mass_storage_class.c, variables.h |
| 0.019g fix LCD port seting | 319 | display_control.c, fpga_control.c, scope_functions.c, statemachine.c, variables.h |
| 0.020 | 4567 | +Menu.c, +Menu.h, scope_functions.c, scope_functions.h, variables.h |
| 0.020a calibrating text | 337 | Menu.c, Menu.h, fnirsi_1013d_scope.c, power_and_battery.c, scope_functions.c, scope_functions.h, statemachine.c, variables.h |
| 0020a last | 103 | Menu.c, scope_functions.c, variables.c, variables.h |
| 0.0201 | 386 | Menu.c, Menu.h, scope_functions.c, statemachine.c, variables.c, variables.h |
| 0.020b | 458 | Menu.c, Menu.h, scope_functions.c, statemachine.c, variables.c, variables.h |
| 0.020c | 206 | Menu.c, Menu.h, scope_functions.c, statemachine.c, variables.h |
| 0.020d | 373 | Menu.c, Menu.h, scope_functions.c, statemachine.c, variables.c, variables.h |
| 0.021d little chage calibration fail=0 and clear backspace in code | 5391 | Menu.c, Menu.h, fpga_control.c, scope_functions.c, scope_functions.h, statemachine.c, variables.c, variables.h |
| 0.021e change offset failed text | 169 | Menu.c, Menu.h, statemachine.c, variables.h |
| 0.021f final calibration | 58 | Menu.c, statemachine.c, variables.h |
| 0.021g main menu change dimension,add color in 50% button touched | 83 | Menu.c, scope_functions.c, statemachine.c, variables.c, variables.h |
| 0.021h MENU move input calibration to SF | 463 | Menu.c, Menu.h, scope_functions.c, scope_functions.h, variables.h |
| 0.021j change calibration function,fix fpga offset to all channel | 348 | Menu.c, fpga_control.c, scope_functions.c, scope_functions.h, variables.h |
| 0.022 fix Auto AC mode acept offset value | 185 | Menu.c, Menu.h, scope_functions.c, scope_functions.h, variables.h |
| 0.022b autosetup origo F | 832 | Menu.c, Menu.h, fpga_control.c, scope_functions.c, scope_functions.h, variables.h |
| 0.022c big change | 207 | Menu.c, fpga_control.c, scope_functions.c, statemachine.c, variables.c, variables.h |
| 0.022d frist release autosetup | 137 | Menu.c, scope_functions.c, variables.c, variables.h |
| 0.022e reducte color | 183 | Menu.c, fnirsi_1013d_scope.h, scope_functions.c, statemachine.c, variables.c, variables.h |
| 0.022f autoset sample frekvenci | 272 | Menu.c, scope_functions.c, variables.h |
| 0.022g move autoset and clear unused comands | 1458 | Menu.c, Menu.h, scope_functions.c, scope_functions.h, variables.c, variables.h |
| 0.022h Work on long time base & max light button,wave view fix grid on & switch fast sensitivity | 373 | Menu.c, Menu.h, fnirsi_1013d_scope.c, fpga_control.c, fpga_control.h, icons.c, scope_functions.c, statemachine.c, variables.h |
| 0.022j test set offset | 130 | fnirsi_1013d_scope.c, icons.c, scope_functions.c, statemachine.c, variables.h |
| 0.022k add test file | 183 | -Menu.c, -Menu.h, fnirsi_1013d_scope.c, fpga_control.c, +menu.c, +menu.h, power_and_battery.c, scope_functions.c, scope_functions.h, statemachine.c, +test.c, +test.h, variables.h |
| 0.022m text reset default value a need calibration, work on long TB, change buffer bar | 313 | DS3231.c, fnirsi_1013d_scope.c, fnirsi_1013d_scope.h, menu.c, menu.h, scope_functions.c, statemachine.c, test.c, test.h, variables.c, variables.h |
| 0.022n | 121 | fnirsi_1013d_scope.c, scope_functions.c, test.c, variables.h |
| 0.022o change autosetup with offset signal | 43 | fnirsi_1013d_scope.c, scope_functions.c, test.c, variables.h |
| 0.022p work on long time base, add percentage bateery | 144 | fnirsi_1013d_scope.c, fnirsi_1013d_scope.h, menu.c, scope_functions.c, statemachine.c, variables.c, variables.h |
| 0.022q long time base switch frst work | 377 | fnirsi_1013d_scope.c, fpga_control.c, menu.c, scope_functions.c, statemachine.c, test.c, test.h, variables.c, variables.h |
| 0.023 long base | 82 | fnirsi_1013d_scope.c, fpga_control.c, fpga_control.h, menu.c, scope_functions.c, test.c, variables.c, variables.h |
| 0.023a long base | 90 | fnirsi_1013d_scope.c, scope_functions.c, test.c, variables.c, variables.h |
| 0.023b | 395 | fnirsi_1013d_scope.c, menu.c, scope_functions.c, statemachine.c, test.c, test.h, variables.h |
| 0.023c displej lib fix rec draw | 141 | display_lib.c, fnirsi_1013d_scope.c, menu.c, scope_functions.c, statemachine.c, test.c, test.h, variables.h |
| 0.023d move to sector 709,fix v & t cursor view | 223 | menu.c, scope_functions.c, statemachine.c, test.c, test.h, variables.c, variables.h |
| 0.023e fix triger and menu button function & long mode | 137 | menu.c, statemachine.c, test.c, test.h, variables.h |
| 0.023f frst trigered long  mode | 135 | fpga_control.c, menu.c, scope_functions.c, statemachine.c, test.c, test.h, variables.c, variables.h |
| 0.023g clear code long base | 262 | scope_functions.c, statemachine.c, test.c, variables.h |
| 0.023h long base triger & oveload div-- to 0 | 144 | fpga_control.c, scope_functions.c, statemachine.c, test.c, variables.h |
| 0.023j | 289 | fnirsi_1013d_scope.c, menu.c, scope_functions.c, statemachine.c, test.c, variables.c, variables.h |
| 0.023k | 26 | scope_functions.c, variables.c, variables.h |
| 0.023l | 135 | fnirsi_1013d_scope.c, scope_functions.c, test.c, variables.c, variables.h |
| 0.023m long mode OK | 75 | menu.c, scope_functions.c, statemachine.c, variables.c, variables.h |
| 0.023n fix time cursor values, fix clear displej in wavview mode on off time and voltage cursors | 94 | scope_functions.c, statemachine.c, variables.c, variables.h |
| 0.023o clear view after waveview mode, fix wav rms value,fix autoset frekvenci | 42 | scope_functions.c, statemachine.c, variables.h |
| 0.023p fixed clear dispej in stop mode, and correct swit stop to run | 37 | scope_functions.c, statemachine.c, test.c, variables.h |
| 0.023q only change runstate on is log1,and waveview zapis | 330 | menu.c, scope_functions.c, statemachine.c, test.c, variables.h |
| 0.023r no wait to connversion | 146 | fnirsi_1013d_scope.c, fpga_control.c, fpga_control.h, scope_functions.c, scope_functions.h, test.c, variables.h |
| 0.023s clear code & fix releoad defaul data | 243 | fpga_control.c, scope_functions.c, statemachine.c, variables.h |
| 0.023t | 23 | menu.c, scope_functions.c, variables.h |
| 0.023u | 18 | fnirsi_1013d_scope.c, menu.c, statemachine.c, variables.h |
| 0.023x change time base number | 210 | fnirsi_1013d_scope.c, fpga_control.c, scope_functions.c, test.c, variables.c, variables.h |
| 0.024 change diag menu | 406 | menu.c, touchpanel.c, variables.c, variables.h |
| 0.024 clear variable.c | 156 | menu.c |
| 0.024a add temperature to diagnostic screen | 282 | DS3231.c, DS3231.h, menu.c, scope_functions.c, variables.h |
| 0.024b add dc offset ch1 | 37 | fpga_control.c, scope_functions.c, test.c, variables.c, variables.h |
| 0.024c add dc offset to ch2, and modifi autoset | 18 | fpga_control.c, menu.c, scope_functions.c, variables.h |
| 0.024d add  corection factor dc shift | 59 | fpga_control.c, scope_functions.c, statemachine.c, statemachine.h, test.c, variables.h |
| 0.024e fixed shit signal in 50mV ON two channel | 72 | scope_functions.c, test.c, variables.h |
| 0.024f add adc compenzation to long mode | 6 | test.c, variables.h |
| 0.024g Autoset in ac mode set offset to 0 | 112 | scope_functions.c, variables.h |
| 0.024h change FPGA average read, add average mode, add buton in menu.c,add A_V mode in menu.c, add texty and offset volt ampere | 510 | fnirsi_1013d_scope.c, fnirsi_1013d_scope.h, fpga_control.c, fpga_control.h, menu.c, menu.h, scope_functions.c, scope_functions.h, statemachine.c, test.c, variables.c, variables.h |
| 0.024i change sample dc shift in FPGA.c | 17 | fpga_control.c, menu.c, variables.h |
| 0.024j auto set ch2 DC fixed | 81 | menu.c, scope_functions.c, variables.h |
| 0.024k change t curzor range, change big frst letter in menus,change position CH menu | 370 | fnirsi_1013d_scope.c, menu.c, statemachine.c, test.c, test.h, variables.c, variables.h |
| 0.024l add more divider | 548 | fpga_control.c, menu.c, scope_functions.c, statemachine.c, variables.c, variables.h |
| 0.024m change chmenu divider | 260 | menu.c, statemachine.c, variables.c, variables.h |
| 0.024n simple sensitivity text | 82 | menu.c, variables.c, variables.h |
| 0.024o ch menu divider ok, triger grafik update, 0 before decimal point fixed | 463 | fnirsi_1013d_scope.c, menu.c, scope_functions.c, statemachine.c, variables.c, variables.h |
| 0.024p fixed calibration switch 0.5 to 1x | 6 | scope_functions.c, variables.h |
| 0.024q move input calibration data to sector 708 sdcard | 235 | menu.c, scope_functions.c, scope_functions.h, statemachine.c, variables.h |
| 0.024r Acuracy coeficient offset, triger horizontal position store in 2x16bit | 85 | fpga_control.c, scope_functions.c, variables.h |
| 0.024s1 clear code  add middle menu ACQ | 387 | fnirsi_1013d_scope.c, menu.c, menu.h, scope_functions.c, scope_functions.h, statemachine.c, statemachine.h, test.c, variables.c, variables.h |
| 0.024s add middle menu ACQ | 151 | menu.c, scope_functions.c, statemachine.c, statemachine.h, test.c, variables.h |
| 0.024t modify autosetup | 323 | fnirsi_1013d_scope.c, menu.c, scope_functions.c, statemachine.c, statemachine.h, test.c, variables.h |
| 0.024x add shift calibration button | 889 | menu.c, scope_functions.c, scope_functions.h, statemachine.c, test.c, variables.c, variables.h |
| 0.024y long memory mode | 340 | fnirsi_1013d_scope.c, menu.c, menu.h, scope_functions.c, scope_functions.h, statemachine.c, test.c, test.h, variables.h |
| 0.025a add menu for calibration dc shift V+ V- | 434 | fnirsi_1013d_scope.c, mass_storage_class.c, menu.c, menu.h, scope_functions.c, statemachine.c, usb_interface.c, variables.h |
| 0.025b add variable to program dc shift | 177 | fpga_control.c, menu.c, scope_functions.c, statemachine.c, variables.h |
| 0.025c fix reset waveform in waveform mode press Time or Voltage cursor and noreset sample settings in start-stop | 26 | fnirsi_1013d_scope.c, fpga_control.c, scope_functions.c, statemachine.c, variables.h |
| 0.025c with new bootloader | 2 | Makefile |
| 0.025d new bootloader | 24 | menu.c, variables.h |
| 0.025e add visible dc shift value | 83 | menu.c, menu.h, statemachine.c, test.c, variables.h |
| 0.025g litle chagne dc shift menu | 16 | statemachine.c, variables.h |
| 0.025h dc shift value mesurents set to 400 | 6 | statemachine.c, variables.h |
| 0.025j change dc shift, sigle mode fix switch sample freq,change reset and restore button | 212 | fnirsi_1013d_scope.c, menu.c, menu.h, scope_functions.c, statemachine.c, test.c, variables.c, variables.h |
| 0.025j no change | 206 | fnirsi_1013d_scope.c, menu.c, menu.h, scope_functions.c, statemachine.c, test.c, variables.c, variables.h |
| 0.025k netestovane | 396 | fnirsi_1013d_scope.c, fpga_control.c, menu.c, menu.h, scope_functions.c, statemachine.c, test.c, variables.c, variables.h |
| 0.025l add boot menu for bootloader fw0.04 | 56 | menu.c, variables.h |
| 0.025m change to dc shift | 83 | fnirsi_1013d_scope.c, scope_functions.c, statemachine.c, test.c, variables.h |
| 0.025n remove fpga command 0x28 and 0x3C, change triger level long mode, fix acq menu long-short switch | 114 | fnirsi_1013d_scope.c, fpga_control.c, fpga_control.h, menu.c, scope_functions.c, test.c, variables.c, variables.h |
| 0.025o change remote sample freq and dime prediv, single triger fix | 287 | fnirsi_1013d_scope.c, fpga_control.c, fpga_control.h, menu.c, scope_functions.c, scope_functions.h, statemachine.c, variables.h |
| 0.025p change memory slider, move rtc text in menu | 93 | fnirsi_1013d_scope.c, fpga_control.c, menu.c, scope_functions.c, variables.c, variables.h |
| 0.025q move diag menu to main menu + icons menu + update batery indicator | 1118 | fnirsi_1013d_scope.c, fpga_control.c, icons.c, menu.c, menu.h, power_and_battery.c, statemachine.c, statemachine.h, test.c, variables.c, variables.h |
| 0.025r move text in diagnostic menu and fix read boot data | 263 | menu.c, menu.h, statemachine.c, variables.h |
| 0.025s lock move cursors | 193 | menu.c, menu.h, scope_functions.c, statemachine.c, variables.h |
| 0.025t move bat value one pixel up,scope_prepare_setup_for_file fix lockcursors,reset comp value | 23 | menu.c, menu.h, scope_functions.c, statemachine.c, variables.h |
| 0.025u add fast switch backlight brightness to max | 128 | fnirsi_1013d_scope.c, menu.c, menu.h, statemachine.c, test.c, variables.h |
| 0.025v save maxlight settings | 434 | display_lib.c, scope_functions.c, scope_functions.h, statemachine.c, test.c, test.h, variables.h |
| 0.025x update code for maxlight settings & change roll mode & fix oveload red line | 370 | display_lib.c, display_lib.h, fnirsi_1013d_scope.c, fpga_control.c, menu.c, menu.h, scope_functions.c, statemachine.c, test.c, variables.h |
| 0.025y1 change calculate freqvency in fpga code | 689 | +_kiss_fft_guts.h, +ch340_class.c, +ch340_class.h, +commands.c, +commands.h, fnirsi_1013d_scope.c, fpga_control.c, +kiss_fft.c, +kiss_fft.h, +kiss_fft_log.h, +kiss_fftr.c, +kiss_fftr.h, scope_functions.c, usb_interface.c, usb_interface.h, variables.c, variables.h |
| 0.026a long memory OFF | 465 | ch340_class.c, ch340_class.h, fnirsi_1013d_scope.c, menu.c, scope_functions.c, statemachine.c, usb_interface.c, usb_interface.h, variables.h |
| FNIRSI_1013D_Firmware-main | 151 | fnirsi_1013d_scope.c, menu.c, menu.h, scope_functions.c, statemachine.c, test.c, usb_interface.c, variables.h |
| 0.026c change button TP, add keyboard | 425 | fnirsi_1013d_scope.c, fnirsi_1013d_scope.h, menu.c, menu.h, scope_functions.c, statemachine.c, test.c, variables.c, variables.h |
| 0.026d | 52 | fnirsi_1013d_scope.c, fpga_control.c, menu.c, scope_functions.c, timer.c, variables.c, variables.h |
| 0.026e top cursor is move, 50mV error fix | 257 | fnirsi_1013d_scope.c, fpga_control.c, menu.c, scope_functions.c, statemachine.c, statemachine.h, variables.c, variables.h |
| 0.026f add invert variable,missing button invert | 33 | fpga_control.c, scope_functions.c, variables.h |
| 0.026h change channel menu, add invert button | 499 | menu.c, menu.h, scope_functions.c, statemachine.c, variables.h |
| 0.026i new autosetup change to work invert mode | 98 | scope_functions.c, variables.h |
| 0.026j rounded_rect menu items | 156 | fnirsi_1013d_scope.h, menu.c, variables.h |
| 0.026k2 pred posunom ACQ, MOVEfast | 212 | fnirsi_1013d_scope.c, menu.c, statemachine.c, variables.h |
| 0.026k3 posunute acq,move fast,light button | 60 | menu.c, variables.h |
| 0.26k4 posun ikony jasu, upraveny make subor with FEL | 49 | Makefile, menu.c, variables.h |
| 0.26k5 - trigger menu change | 129 | menu.c, menu.h, statemachine.c, variables.h |
| 0.26l move triger button and menu | 380 | fpga_control.c, menu.c, menu.h, statemachine.c, variables.h |
| 0.26m add ref1 & ref2 | 498 | fnirsi_1013d_scope.c, fnirsi_1013d_scope.h, menu.c, menu.h, scope_functions.c, scope_functions.h, statemachine.c, variables.c, variables.h |
| 0.26n grafik change on triger menu | 315 | fnirsi_1013d_scope.h, menu.c, statemachine.c, variables.h |
| 0.26o change triger menu | 159 | menu.c, menu.h, statemachine.c, variables.h |
| 0.26p1 add ref and math menu | 740 | menu.c, menu.h, statemachine.c, statemachine.h, test.c, variables.c, variables.h |
| 0.26p2-add define all menus | 143 | menu.c, statemachine.c, variables.c, variables.h |
| 0.26p3-display copy work on ref & math menu | 118 | menu.c, menu.h, statemachine.c, statemachine.h, variables.c, variables.h |
| 0.26p4 other menu start creation | 217 | menu.c, menu.h, statemachine.c, variables.c, variables.h |
| 0.26p5 other menu work with settings | 510 | menu.c, menu.h, scope_functions.c, statemachine.c, variables.c, variables.h |
| 0.26p6 other settings OK | 33 | menu.c, statemachine.c |
| 0.26p7 problem with channel menu touch | 109 | menu.c, menu.h, statemachine.c, variables.c, variables.h |
| 0.26p11 if else solved | 432 | icons.c, menu.c, menu.h, statemachine.c, test.c, variables.h |
| 0.26p change channel sensitivity menu | 1801 | icons.c, menu.c, menu.h, scope_functions.c, statemachine.c, statemachine.h, test.c, variables.c, variables.h |
| 0.26q1 icons done | 1877 | icons.c, menu.c, menu.h, scope_functions.c, statemachine.c, statemachine.h, test.c, variables.c, variables.h |
| 0.26q2 finis switch x-y and notifi | 19 | menu.c |
| 0.26q3 add generator button | 261 | menu.c, menu.h, usb_interface.c, variables.h |
| 0.26q4 beta test | 234 | menu.c, scope_functions.c, statemachine.c, variables.h |
| 0.26q6 ref1-ref4 ok | 1008 | fnirsi_1013d_scope.c, fnirsi_1013d_scope.h, menu.c, menu.h, scope_functions.c, statemachine.c, variables.c, variables.h |
| 0.26q7 sum and diff | 156 | menu.c, menu.h, scope_functions.c, statemachine.c, test.c, test.h, variables.h |
| 0.26q icons done | 1541 | fnirsi_1013d_scope.c, fnirsi_1013d_scope.h, menu.c, menu.h, scope_functions.c, statemachine.c, test.c, test.h, usb_interface.c, variables.c, variables.h |
| 0.26r2 BMP time stamp | 2537 | fnirsi_1013d_scope.c, fnirsi_1013d_scope.h, menu.c, menu.h, +ref_and_math.c, +ref_and_math.h, scope_functions.c, statemachine.c, test.c, test.h, usb_interface.c, variables.c, variables.h |
| 0.26r5 save ref ok | 653 | DS3231.c, menu.c, ref_and_math.c, ref_and_math.h, scope_functions.c, statemachine.c, test.c, test.h, variables.c, variables.h |
| 0.26r6 save  and load ref | 263 | ref_and_math.c, ref_and_math.h, statemachine.c |
| 0.26s2 add 2 button to other menu, fix save index and wave index error | 1048 | DS3231.c, menu.c, menu.h, ref_and_math.c, scope_functions.c, scope_functions.h, statemachine.c, test.c, test.h, variables.c, variables.h |
| 0.26s3 clear rtc button wait touch | 42 | statemachine.c |
| 0.26s chagne ref1_4 to cannel ref1_4 | 411 | DS3231.c, menu.c, menu.h, ref_and_math.c, scope_functions.c, scope_functions.h, statemachine.c, test.c, test.h, variables.c, variables.h |
| 0.26u6 rozorobene menu na ref8 | 1847 | DS3231.c, display_lib.c, display_lib.h, fnirsi_1013d_scope.c, fnirsi_1013d_scope.h, icons.c, menu.c, menu.h, ref_and_math.c, ref_and_math.h, scope_functions.c, scope_functions.h, statemachine.c, test.c, test.h, +usb_cdc.c, variables.c, variables.h |
| 0.26u8 beta verzia | 293 | menu.c, ref_and_math.c, statemachine.c, variables.h |
| 0.26u9 ref auto load on start, fix save and load ref | 267 | menu.c, menu.h, ref_and_math.c, ref_and_math.h, scope_functions.c, statemachine.c, variables.h |
| 0.26v2 touch move ref1-ref8 | 713 | fnirsi_1013d_scope.h, menu.c, ref_and_math.c, scope_functions.c, statemachine.c, statemachine.h, variables.c, variables.h |
| 0.26v5 faster novoment cursors | 447 | fnirsi_1013d_scope.c, menu.c, ref_and_math.c, scope_functions.c, statemachine.c, test.c, variables.h |
| 0.26y5 work ch340 ok | 1965 | DS3231.c, DS3231.h, Makefile, -_kiss_fft_guts.h, +build_time.h, ch340_class.c, ch340_class.h, -commands.c, -commands.h, fnirsi_1013d_scope.c, fpga_control.c, -kiss_fft.c, -kiss_fft.h, -kiss_fft_log.h, -kiss_fftr.c, -kiss_fftr.h, mass_storage_class.c, menu.c, menu.h, ref_and_math.c, scope_functions.c, scope_functions.h, statemachine.c, test.c, test.h, touchpanel.c, -usb_cdc.c, usb_interface.c, usb_interface.h, variables.c, variables.h |
| 0.27i CDC serial working, switch MSC to CDC | 526 | build_time.h, ch340_class.c, ch340_class.h, fnirsi_1013d_scope.c, mass_storage_class.c, mass_storage_class.h, menu.c, usb_interface.c, usb_interface.h, variables.h |
| 0.27j displej 2512data | 87 | build_time.h, ch340_class.c, usb_interface.c, variables.h |
| 0.27m usb_cdc_set_line_coding OK | 801 | +PC_interface.c, +PC_interface.h, build_time.h, +cdc_class.c, +cdc_class.h, -ch340_class.c, -ch340_class.h, fnirsi_1013d_scope.c, usb_interface.c, usb_interface.h, variables.h |
| 0.27p change buffer CDC to 512 and callback buffer 512 | 629 | PC_interface.c, PC_interface.h, build_time.h, cdc_class.c, cdc_class.h, usb_interface.c, usb_interface.h, variables.h |
| 0.27r YES work it with APP | 138 | PC_interface.c, PC_interface.h, build_time.h, cdc_class.c, usb_interface.c, usb_interface.h, variables.h |
| 0.27s clear CDC settings and move callback to main loop | 198 | PC_interface.c, build_time.h, cdc_class.c, cdc_class.h, fnirsi_1013d_scope.c, usb_interface.c, usb_interface.h, variables.h |
| 0.27t clear  usb interface kode | 525 | PC_interface.c, PC_interface.h, build_time.h, cdc_class.c, statemachine.c, test.c, usb_interface.c, variables.h |
| v1.00g | 2202 | PC_interface.c, PC_interface.h, build_time.h, fnirsi_1013d_scope.c, fpga_control.c, fpga_control.h, menu.c, menu.h, ref_and_math.c, scope_functions.c, statemachine.c, usb_interface.c, usb_interface.h, variables.c, variables.h |
| v1.00h clear gen menu, and little grafik corection | 118 | build_time.h, menu.c, menu.h, scope_functions.c, usb_interface.c, variables.h |
| v1.00i statemachine button add for gen menu | 338 | build_time.h, menu.c, statemachine.c, test.c, variables.h |
| v1.00j work on keyboard,move menus to Y48, add menu open flags | 399 | build_time.h, fnirsi_1013d_scope.c, menu.c, menu.h, statemachine.c, statemachine.h, usb_interface.c, variables.c, variables.h |
| v1.00l3 zadavanie OK,treba doprogreamovat obmedzenia napr duty max 100% | 607 | PC_interface.c, PC_interface.h, build_time.h, +generator.c, +generator.h, menu.c, menu.h, scope_functions.c, statemachine.c, statemachine.h, test.c, usb_interface.c, variables.h |
| v1.00l5 fix error on signal 25 or 75% triger | 279 | build_time.h, fnirsi_1013d_scope.c, menu.c, scope_functions.c, statemachine.c, variables.h |
| v1.00lv1 zadavanie z klavesnice funguje, treba dokoncit pre freq duty hold off a ostatne | 599 | build_time.h, fnirsi_1013d_scope.c, generator.c, generator.h, menu.c, scope_functions.c, statemachine.c, variables.h |
| v1.00o5 | 2695 | Makefile, build_time.h, display_control.c, fnirsi_1013d_scope.c, fnirsi_1013d_scope.h, fpga_control.c, generator.c, generator.h, menu.c, menu.h, scope_functions.c, scope_functions.h, statemachine.c, usb_interface.c, variables.c, variables.h |
| 0.28a (compared with 1.00o5, i.e. generator.c is absent here) | 5052 | Makefile, PC_interface.c, PC_interface.h, build_time.h, display_control.c, fnirsi_1013d_scope.c, fnirsi_1013d_scope.h, fpga_control.c, fpga_control.h, -generator.c, -generator.h, menu.c, menu.h, ref_and_math.c, scope_functions.c, scope_functions.h, statemachine.c, statemachine.h, test.c, usb_interface.c, usb_interface.h, variables.c, variables.h |
| 0.26y4 (repo folder "Scope 1013D v0.26y4 clear") | n/a | sibling of 0.26y5; diff vs 0.28a not meaningful |

## 2. Documentation

### 2.1 Manual: `Guide to firmware/FNIRSI 1013D  alternative firmware v1.00n3v ANG v1 – kópia.doc`
8.8 MB Word 97 file; text extracted with antiword (672 lines). 22 `[pic]` placeholders omitted (embedded images; `Guide to firmware/pictures/` holds 13 loose images 1.png..10.jpg, 4c/4d/6a/6b, not inspected). Title: "Manual for firmware of FNIRSI 1013D oscilloscope (YAPOK or DANIU)"; "You do everything at your own risk"; "Thanks to the creators: PC and Atlan"; support thread https://www.eevblog.com/forum/testgear/fnirsi-1013d-100mhz-tablet-oscilloscope/2450/ .

**Added features vs original firmware**
- Startup boot menu: Alternative PECO / FNIRSI / FEL mode.
- Optional RTC: date/time in image preview, timestamps in saved files, clock on main screen (needs RTC module hardware).
- Adjustable sampling frequency and time/div (more samples, e.g. see more I2C packets).
- Always 3000 samples per channel (original 1500 or 3000); with firmware v1.00+ maybe 16000 samples.
- Progress across the entire buffer (0..3000).
- Separate voltage cursor values per channel, delta Y.
- Refined measurement menu with quick select (select all, clear, hide).
- Quick sensitivity setting from channel menu.
- Trigger level separate for CH1 and CH2.
- Single trigger: tap above the center axis changes sample freq AND time/div; below the axis only time/div.
- AD converter overrange shown as red curve.
- Works from 0 mV (original does not display signals below/above 6 signal units, e.g. at 100 mV/div shows only > 30 mV).
- Firmware stored on SD card (original is in flash on PCB); many original bugs removed; open source; regular updates (current "v0.027u"); FFT not yet available.

**Firmware backup (do before flashing alternative firmware)**: forum page 45 (https://www.eevblog.com/forum/testgear/fnirsi-1013d-100mhz-tablet-oscilloscope/1100/). Backup program `fnirsi_1013d_fwb.bin` (in `Firmware for create backup files/`): saves touch-panel config; detects flash maker/id to back up 2 MB or 4 MB (min 2 MB since original is 1.6 MB); when finished shows as USB mass storage to copy files; shows messages for special touch panel. Creates 3 files on the SD card: `FNIRSI_1013D_full_flash_backup.bin`, `FNIRSI_1013D_tp_config.bin`, `FWB_FSI-1013.bin`. Video: youtube h-tdKPDocHI.

**Firmware update / SD layout**: firmware lives in a reserved area at the start of the SD card, sectors 0..2048 (file `fnirsi_1013d.bin` at byte offset 8 KiB: `dd ... bs=1024 seek=8`). Scope settings from sector 709; input-divider calibration in sector 708; display/TP/boot configuration from sector 710. Write with Linux console (dd) or Windows LOADER program. An 8 GB card by default has a 1 MB reserved gap; if missing (or small card) LOADER (>= 1.1.1) errors on write, so repartition. Windows fix (from forum page 62): check partition offset with DISKPART (`LIST DISK; SELECT DISK n; SELECT PARTITION 1; DETAIL PARTITION`, "Offset in Bytes"); 1048576 is default for >= 8 GB; reformatting does not change an existing small offset -> delete partition and recreate (`CLEAN; CREATE PARTITION PRIMARY`, or Disk Management: remove volume, new simple volume), then format FAT32; 1-4 GB cards default to offset 65536 = not enough space without special repartition. Linux: video youtube 4yFmStYuK_o ("fix sdcart problem with load firmware").

**Calibration (order)**: 1 charge scope; 2 power on >= 5 min warm-up; 3 start basic calibration; 4 calibrate input dividers (via diagnostics menu); 5 calibrate DC offset (to channel cursor, to zero signal, to zero measured value); 6 back up the data with LOADER >= 1.1.3 or on-scope UPDATE interface.
- Input dividers: video KK51iftmdVU. On firmware upload you are warned calibration is needed; if already calibrated and the new firmware needs none, a "reset to default" is offered on the right. Menu entry "Base line calibration". Calibration voltages: 100 mV/div -> 300 mV; 200 mV/div -> 600 mV; 500 mV/div -> 1.5 V; 1 V/div -> 3 V; 2.5 V/div -> 7.5 V; 5 V/div -> 15 V (ripple-free analog source or battery). Re-calibration via Diagnostic menu, button RESET calibration data; RESTORE restores saved values (unless overwritten by power-off or new calibration).
- DC shift calibration: video rAgULKzP3MU. 1) set signal position, ALWAYS align to cursor of channel 1 or 2 (position only set in second step); 2) zero signal setting; 3) Vmin set to 0 mV. (Video aligned to the signal, which the author says was a mistake; one parameter must then be re-adjusted.)
- Diagnostic screen reached from main menu item "Diagnostic view".

**Other settings menu**: 1 Boot menu (green = shown at startup); 2 Default start (PECO, FNIRSI, FEL; bootloader >= 0.04: 0 PECO, 1 FNIRSI, 2 FEL; boot menu always active for FNIRSI and FEL; screen not touched -> default after 3 s, touch extends 10 s); 3 New autosetup (ON: autoset tries all sensitivities at each time/div change; OFF: sensitivity increases gradually, >1 MHz low-amplitude signals may be missed); 4 Lock move cursors (time cursor: right free, left moves both; voltage cursor: lower free, upper moves both); 5 DC shift calibration (ON: right-hand menu on main screen, CTRL shows DC-shift parameters; turn it off after setting); 6 USB serial mode (scope appears as serial port for terminal commands; diagnostics/development); 7 DEV mode (diagnostics/development, DO NOT USE; allows settings that may damage/freeze the scope); 8 Add tag in BMP (timestamp in screenshots); 9 Load REF to start (reference waveforms with their parameters auto-loaded at power-on). Updater v0.02v screen is reachable via the UPDATER button on the boot screen (enabled in other settings).

**Measurement menu**: Vmax, Vmin, Vavg, Vrms, Vpp, Freq, Cycle (period), Tim+ (rising edge time), Tim- (falling edge time), Duty+, Duty-. Options: Select All, Clear, Hide, Ref1-Ref4 / Ref5-Ref6 (measure reference channels).

**ACQ menu** (DOT trace mode variant shown): sample frequency; time/div; Average mode (not available in single trigger or Long/scroll mode); quick switch ACQ short <-> Long; Long memory ("not working"); Memory depth (only with new FPGA firmware); Trace type linear / dots / peak / pro (PRO recommended).

**TRIGGER menu**: mode Single (one shot) / Auto / Normal; edge Rising/Falling; channel CH1, CH2 or Ext (EXT future); position 5 % / 50 % / 75 % (only new FPGA firmware); Hold off ("not working"); Always 50 % (trigger always to 50 %; if ON do not move trigger cursor elsewhere).

**Generator menu** (ONLY with new FPGA firmware): Power (enable output; future: switch between EXT input and generator output), Config (internal/external source, future), Wave Square/Sine/Triangle (hardware supports only SQUARE), Freq 0 Hz to 10 MHz, Duty 0-100 %, Ampl (hardware not supported, for external generator), Offset (same), Phase (same; described as "amplitude" in the manual, copy/paste).

**Long mode (scroll)**: long time base 100 ms/div to 50 s/div (waveform view mode: 20 ms to 50 s/div, multiple zoom options); uses memory of 3000 samples per channel; measured-values menu not displayed in long mode; trigger Normal and Single with level + edge; Auto = free run, not triggered.

**Channel / Ref / Math menus**
- Channel: on/off, Open FFT (not yet), invert, DC/AC, Volt/Ampere mode, probe ratio 0.5-1-10-20-50-100-1000, quick sensitivity 50 mV/div..5 V/div, quick trigger 50 %.
- Ref: on/off ref1-ref4 (ref5-ref6 in text; menu supports ref8 in code), Chx to Refx (copy current waveform to Ref buffer), Load Refx (SD card -> buffer), Save Refx (buffer -> SD), Off All, Clear All, Load All, Save All.
- Math: functions A+B, A-B, A*B, A/B, OFF; CH1 => A and CH2 => B select the math source buffers.

**RTC menu**: switch RTC module on/off and set time/date (only with hardware mod; "See hardware improvements").

**Hardware MODs**: (1) real-time clock circuit, DS3231/"DS3132" module with battery (video YgxN9rrtkDI "Assembly of the RTC module"); (2) step-up converter (AL142, FP6291 or MT36291) ahead of the MIC29302 stabilizer: cut the marked PCB trace to the stabilizer and feed it 5 V from a Li-Po boost converter; 5 V must NOT reach the display inverter; benefit: traces do not move when battery voltage drops.

**Data layout (manual table)**: `INPUT_CALIBRATION_SECTOR 708` (word 0-1 checksum); `INPUT_CALIBRATION_SETTING_OFFSET 8` (256 bytes sector, used 14x32 bit = 28 B at 8-40); `DC_SHIFT_SETTING_OFFSET 40` (used 6x32 bit = 12 B at 40-52; bytes 8-52 checksummed); `SETTINGS_SECTOR 709` (old 700). Offsets (words of 16 bit, "All bytes / used"): CHANNEL1 @8 (24 / 10x16), CHANNEL2 @32 (24 / 10x16), TRIGGER @56 (24 / 9x16), OTHER @80 (24 / 13x16), CURSOR @104 (16 / 6x16), MEASUREMENT @120 (32 / 26x16), CALIBRATION @152 (16 / 12x16), CALIBRATION_C @168 (8 / 4x16), next @176; "Use File setup" column: ch1 20x16, ch2 20x16, trigger 11x16, other 10x16, cursor 6x16, measurement 26x16. (Latest code, variables.h v0.28a, uses different offsets: CH1 8, CH2 32, TRIGGER 56, OTHER 80 (40 B), CURSOR 120, MEASUREMENT 136, CALIBRATION 168, CALIBRATION_C 184; SETTING_SECTOR_VERSION 0x0100 / 0x0014.)

**Not in the manual**: no PC-application section and no mention of the generator keyboard entry. The only "PC" content: "USB serial mode" (CDC) and credit "PC and Atlan". PC app presumably external (see 1.3, `#ECHO` protocol stub, SysParam struct).

### 2.2 Readmes in `Guide to firmware/` and others
- `Guide to firmware/readme`, `firmware_FNIRSI/readme` (Transition folder): one line, "No one is responsible for anything, you do everything at your own risk."
- `Guide to firmware/Frst use (install) firmware/readme`: use Loader 1.1.3 and upload `fnirsi_1013d.bin`; this DELETES calibration data and TP/screen data (no issue for a new user). Later updates via the UPDATER button on the boot screen (enabled in other settings; video 3iCg5SM3Y_8); then power on and choose UPDATE: the updater lets you move/copy files to the scope SD card and upload them with UPLOAD. `data.cal` = input divider calibration (DOWNLOAD saves to SD as backup, UPLOAD restores); `TP&screen.bin` = TP and screen settings (same). Copy `updater.bmp` directly onto the SD card. Folder contains `fnirsi_1013d.bin` (695 368 B), `updater.bmp` (768 070 B).
- `Guide to firmware/firmware_Atlan_Peco/readme`: if UPDATER version < 0.02l you must use `updater.bin` first; only then can you use *.bin files with version designations. Same UPDATE/data.cal/TP&screen.bin/updater.bmp/scope.bmp text. Files: `bootloader v0.7.bin`, `updater v0.02r.bin`, `updater.bin`, `scope v0.26y2.bin`, `scope v0.27u.bin`, `scope.bmp`, `updater.bmp`.
- `Guide to firmware/Transition from version (0.0x - 0.26v7) to new botloader, updater, scope/readme`: if an older alternative firmware is installed, update to 0.26v (ideally v0.26v..0.26v7), then load `fnirsi_1013d_migration.bin` via Loader 1.1.3. BEFORE that it is MANDATORY to enable booting with the startup selection (PECO, FEL, FNIRSI) in the scope menu; power-cycle to verify the selection screen appears. Then choose UPDATE and copy files as above (also scope.bmp). Migration bins: see section 0. Subfolder `firmware_FNIRSI/` has `Fnirsi 1013D.bin` (1 735 254 B, original firmware image to restore via FNIRSI boot option) and a one-line readme.
- `Guide to firmware/FNIRSI 1013D picture convertor/readme.txt`: program finds an image, converts and saves `scope.bmp`; input image 800x480, 565 RGB (16 or 24 bit); copy image into program folder, run `Scope_picture_convertor` (also `Scope_picture_convertor.py`; script embeds a 70-byte BMP header: 800x480, top-down, 16 bpp, BI_BITFIELDS, image size 768 000, file 768 070 B).
- `Recovery firmware/Readme`: disconnect all BNCs; upload the firmware (`Recovery firmware/fnirsi_1013d.bin`, 261 192 B); power off; power on; wait until the test runs on all ranges; after text "Saved file" choose the USB menu and upload the new firmware; power-cycle, new firmware runs.
- `Loader problem/Device write access denied.txt`: same DISKPART/partition-offset text as the manual (Windows) and Linux video 4yFmStYuK_o; author suggests Loader should check for "hidden" free space on the SD. Folder also holds a saved forum page (`...Page 62.html` + `_subory/`).
- `Firmware for create backup files/Firmware for restore backup/Navod na obnovu zalohy.txt` (Slovak + English, pecostm32 post msg5749727): restore program must be installed on the SD card like the open-source firmware; looks for `FNIRSI_1013D_main.bin` or `FNIRSI_1013D_full.bin` on the card top directory and writes it to flash with NO validity check; `_main` goes to flash address 0x00027000, `_full` starts at 0x00000000; program removes itself from the SD after writing; scope then boots original firmware. `_main` firmware attached was modified to leave the touch panel alone. Files: `fnirsi_1013d_fwr.bin.txt` (restore program), `FNIRSI_1013D_main.bin.txt`.
- `.../Firmware for restore backup file TP/Readme.txt` (forum post /2050/): two utilities (derived from Atlan TPx code) writing `FNIRSI_1013D_tp_config.bin` (as produced by `fnirsi_1013d_fwb.bin`, no changes needed) back to the scope; error and no write if file missing; `write_fnirsi_1013d_tp_config.bin` and `write_fnirsi_1013d_tp_config_save.bin` (also stores `my_tp_config.bin` on the SD).
- `Verzia 1.xxx/FPGA for 1.xxx/readme`: "Install first bootloader v0.8 or higher".
- `loader_v1.1.3` (`FNIRSI_Firmware_Loader.exe`, `readme_v1.1.3.txt`): v1.0.0 load/verify/delete firmware on the USB-connected scope (Allwinner F1C100S shown as default device) or SD card in a reader, read/write/clear/compile/fix LCD/TP configuration sector; writes physical sectors directly (refuses non-USB, non-512 B sectors, system volumes, MBR/partition areas); needs admin rights. v1.1.0: LCD/TP config default extension `.cnf`; shows partition type/hidden sectors (red if not FAT32 or too few; writes disabled); Calibration page to save/load/clear sector 708 (`.cal`). v1.1.1: UI fixes; default calibration file name = date yyyy-mm-dd; first repartition procedure (MBR overwritten, one FAT32-LBA partition with required hidden sectors; correct count = 1023 or more for the alternative firmware; destructive, unfinished; does not work on GPT cards); reformat not working. v1.1.2: "Boot Mode" page (which firmware loads, boot menu). v1.1.3: buttons to write/clear boot-mode byte; warning when FEL mode and boot menu disabled.
- Root `README.md` (Atlan4): "download GUIDE for firmware and read manual (manual, loader 1.1.3, latest firmware)"; no responsibility; info thread https://www.eevblog.com/forum/testgear/fnirsi-1013d-100mhz-tablet-oscilloscope/msg5172177/#msg5172177 ; RTC module not required (disabled by default; not included with the scope); touch problems -> `configuration_file.txt`; USB upload under Windows: unzip LOADER, run, power scope, connect USB, enable USB in scope menu, after ~10 s Windows opens a folder window, in Loader select BIN, upload, touch screen to switch off USB, power off scope; video LVI45oJHTK8; CALIBRATION: use instructions, notification-confirmation option must be on, calibrate without USB and probes, connect the required voltage to BOTH channels at once, after finishing close menu and power off to save the data; Loader can revert to previous firmware, new firmware lives only on the SD card while the original stays in flash (safe approach: use a second SD card); original source https://github.com/pecostm32/FNIRSI_1013D_Firmware ; `modify 1014d startup for 1013d.zip` = tool to edit the bootloader in the 1014d folder and adjust/modify the 1013d .bin in the 1013 folder (bootloader modified for 1013D: touch display support added, UART for 1014 removed).
- `Scope*/dist/Debug/GNU_ARM-Linux/readme` (pecostm32 wording): `fnirsi_1013d.bin` = firmware to load onto SD; `fnirsi_1013d_scope.bin` = load to DRAM via sunxi-fel in FEL mode for testing without writing the card; other .bin = configuration files for display/touch hardware variants.
- `How_to_load_scope.txt`: `sudo ./sunxi-fel version`; `sudo ./sunxi-fel -p write 0x7FFFFFE0 fnirsi_1013d_scope.bin exe 0x80000000`; SD: `sudo umount /dev/sdc1; sudo dd if=fnirsi_1013d.bin of=/dev/sdc bs=1024 seek=8`; display config: `dd if=shifted_display_config_sector.bin (or standard_display_config_sector.bin) of=/dev/sdc bs=1024 seek=355`; remove program: `dd if=/dev/zero of=/dev/sdc bs=1024 seek=8 count=1`.

### 2.3 `configuration_file.txt` (display + touch config sector 710)
- Purpose: scope hardware varies (different LCD and touch panels); one firmware for all via a configuration file in sector 710 (512 B), written at 355 KiB: `sudo dd if=standard_display_tp_xy_swap_config_sector.bin of=/dev/sdc bs=1024 seek=355` (unmount first: `sudo umount /dev/sdc1`).
- Standard display + TP data line (30 bytes, little endian): `AA AA AA AA 55 55 55 55 4A 00 1E 04 17 00 1A 04 CC CC CC CC 33 33 33 33 5F 00 38 08 FF FF`.
- Layout: bytes 0-7 preamble (u32 0xAAAAAAAA, 0x55555555); 8-11 horizontal timing (u32 0x041E004A); 12-15 vertical timing (u32 0x041A0017); 16-23 postamble (u32 0xCCCCCCCC, 0x33333333); 24-27 checksum = plain unsigned sum of the first six u32 values (here 0x0838005F); 28-29 touch-panel swap flags: byte 28 = X, byte 29 = Y, 0xFF = no swap, 0x00 = swap coordinates (added later; NOT covered by the checksum). Validation scheme applies to display data only.
- Seen values: 1014D new 0x041E0043/0x041A0019; 1014D old 0x041E006D/0x041A0022; 1013D v1 0x041E0044/0x041A0017; 1013D v2 0x041E004A/0x041A0017 (most common); 1013D v3 0x041E0072/0x041A001F.
- Note in repo: `Scope*/dist` has `mstandard_display_config_sector.bin`, `shifted_`, `standard_`, `standard_display_tp_xy_swap_` images (512 B each).

### 2.4 `waveform_file_format for v0.026b.txt` (exact content summary)
- File = settings + signal data. First 800 bytes: settings as little-endian 32-bit integers. Then channel 1 signal data at 0x320..0xED7 (3000 bytes), then channel 2 data at 0xED8..0x1A8F (3000 bytes). All samples are single bytes (total 6800 = 0x1A90 bytes).
- Checksum: sum of 32-bit integers over all data, starting at 0, includes unused in-between words; sample data added in 4-byte chunks. Reference implementation: `void scope_prepare_setup_for_file(void)` in `scope_functions.c`. Stray remark in file: "8fc stred, 8f8 triger ch1".
- Header text "v0.026d"; table (byte offset: value, channel; x = common):

| offset | value | ch |
|---|---|---|
| 0 | Checksum | x |
| 4 | File version | x |
| 32 | Enable (0/1) | 1 |
| 36 | Display volt per division (range 0-6) | 1 |
| 40 | Sample volt per division (range 0-6) | 1 |
| 44 | FFT enable (0/1) | 1 |
| 48 | AC/DC coupling (0 DC, 1 AC) | 1 |
| 52 | Probe magnification | 1 |
| 56 | V_A (volt/ampere) | 1 |
| 60 | Trace position on screen | 1 |
| 64 | Minimum measured | 1 |
| 68 | Maximum measured | 1 |
| 72 | Average measured | 1 |
| 76 | RMS measured | 1 |
| 80 | Center value measured | 1 |
| 84 | Peak-peak value measured | 1 |
| 88 | Frequency valid | 1 |
| 92 | Frequency determined | 1 |
| 96 | Average low time measured | 1 |
| 100 | Average high time measured | 1 |
| 104 | Average period time measured | 1 |
| 128..200 | same 19 fields in the same order (128 Enable ... 200 Average period time) | 2 |
| 224 | Time per division | x |
| 228 | Sample rate | x |
| 232 | Trigger mode | x |
| 236 | Trigger edge | x |
| 240 | Trigger channel | x |
| 244 | Trigger level | x |
| 248 | Trigger horizontal position | x |
| 252 | Trigger horizontal position (second entry, as listed) | x |
| 256 | Trigger vertical position | x |
| 260 | Display have trigger | x |
| 264 | Display trigger index | x |
| 320 | Move speed | x |
| 324 | Right menu state | x |
| 328 | Screen brightness | x |
| 332 | Grid brightness | x |
| 336 | Always 50% trigger | x |
| 340 | X-Y display mode | x |
| 344 | Confirmation mode | x |
| 348 | Long mode | x |
| 352 | Average mode | x |
| 356 | Long memory | x |
| 416 | Time cursors enable | x |
| 420 | Volt cursors enable | x |
| 424 | Time cursor 1 position | x |
| 428 | Time cursor 2 position | x |
| 432 | Volt cursor 1 position | x |
| 436 | Volt cursor 2 position | x |
| 440 | Lock cursors | x |
| 480 | Hide values | 1 |
| 484 | Hide values | 2 |
| 488..532 | Vmax, Vmin, Vavg, Vrms, Vpp, Vp, Freq, Cycle, Time+, Time-, Duty+, Duty- enables (4-byte steps: 488 Vmax, 492 Vmin, 496 Vavg, 500 Vrms, 504 Vpp, 508 Vp, 512 Freq, 516 Cycle, 520 Time+, 524 Time-, 528 Duty+, 532 Duty-) | 1 |
| 536..580 | same 12 enables (536 Vmax ... 580 Duty-) | 2 |

- Real code differs from the 0.026b/d description (checked in v0.28a / v0.26y4 source): `WAVEFORM_FILE_VERSION = 0x01000002` ("Version 1.0.0.2") stored at word 1 (offset 4); VERSION_STRING first 7 chars stored at offset 8 (word 2-3); `VIEW_NUMBER_OF_SETTINGS = 200` words (800 B, matches); word-index offsets in v0.28a code: CHANNEL1 8 (=byte 32), CHANNEL2 32 (=128), TRIGGER 56 (=224), OTHER 80 (=320) match the table, but CURSOR 120 (=480) and MEASUREMENT 136 (=544) differ from the 0.026d table (cursors 416, measurements 480+), i.e. the layout changed after 0.026d; v0.28a writes `SAMPLE_COUNT` (16382) bytes per channel instead of 3000 (v0.26y4 and earlier: 3000); ref/wave load rejects files whose word 1 != WAVEFORM_FILE_VERSION (`MESSAGE_WAV_VERSION_MISMATCH 12`). I did not fully re-derive every offset above against the newest code; the table is the author's 0.026d document.

### 2.5 `new files 1013d/todo.txt` and `Scope*/...` extras
Covered in Part 3 (it is pecostm32's working todo, ~1675 lines, mostly code scraps after the first ~150 lines).

## 3. TODOs and known issues

### 3.1 Documented limitations (manual / folder notes)
- Manual: Long memory "not working"; Hold off "not working"; FFT not yet available (kiss_fft files present since 0.025y1); EXT trigger source and generator Config/Power-to-EXT "future"; generator Sine/Triangle, Ampl, Offset, Phase not supported by hardware; Memory depth and trigger position 5/75 % only with new FPGA firmware; Average not available in single/long mode; long mode hides the measurement menu; DEV mode dangerous; 3000 samples/channel (16000 maybe with 1.00+).
- 1.00l3: "need to code limits e.g. duty max 100 %" (done in o5: clamp 0..100); 1.00lv1: "finish keyboard entry for freq, duty, hold off and others" (hold off still not wired in o5).
- FPGA readme (EF2): restoring the original EF2 bitstream is "not yet possible".
- loader v1.1.1 notes: reformat not working, repartition destructive/unfinished, GPT unsupported.
- v0.26y2 comment: "test generate CSV file with slow fast button".

### 3.2 TODO/FIXME/XXX comments in latest sources (v0.28a and v1.00o5)
- `grep -E "TODO|FIXME|XXX|HACK|BUG"` finds NO such markers in either tree (excluding ff.c, ffunicode.c, fonts, build). Only soft markers (Slovak "treba?" = "needed?", "netreba" = "not needed"):
  - v0.28a: menu.c:2961,2964 `// netreba ?` (commented draw calls); scope_functions.c:1425 `//scope_do_50_percent_trigger_setup(); //netreba lebo triger off` (trigger off); scope_functions.c:2519 "On the scope the last pixel interleaving is not working properly. Don't know why." (pecostm32 comment); test.c:787 `// treba ?`; usb_interface.c:353 `//treba?`; usb_interface.c:1593 `usb_tx_busy = 0; // az ked sa vsetko dokonci` (reset only after all is done); usb_interface.h:450 `usb_write_ep2_data ... ///treba?`.
  - v1.00o5: same set at shifted lines (menu.c:3127,3130; scope_functions.c:1446, 2556; test.c:787; usb_interface.c:354; usb_interface.h:452) plus menu.c:6368,6371,6499 `//treba? 21.11.2025` (commented font/color lines) and statemachine.c:388,421 `//Restore the screen when done //netreba je v close_open_channel_menus`.
- Other leftovers: `fpga_control.c` o5 has a commented-out block with new fixed-point frequency calculation; generator `fpga_set_generator_offset` call commented out; hold-off value write commented out; `PC_interface.c` only `#ECHO` (handler body commented), no real PC protocol implemented in this tree.
- v0.28a vs 1.00o5 are divergent branches (28a: fixed 16000 divider, 0x1B/0x1C, no generator; o5: fw_FPGA detection, generator, EF2 6144 circular buffer); `variables.h` comment in 0.28a: `SETTING_SECTOR_VERSION_LOW 0x0014 //>0x0002 //nova verzia ma 2 na 709 sector` (new version has "2" in sector 709).

### 3.3 `new files 1013d/todo.txt` (pecostm32's, untouched by Atlan4; copied into repo)
Header: "Files modified for baseline calibration: statemachine.c line 936 (calibration call), scope_functions.c/.h and fpga_control.c/.h several functions added." Also a Wireshark filter to reject keyboard and mouse (`!(usb.dst == "1.2.1") && ...` for 1.2.1..1.2.3), a note where USB sticks and scope are kept (Dutch), resize-raw-SD-image recipe (losetup/partprobe/gparted/truncate/chown) and `dd if=/dev/zero of=/dev/sdc bs=1024 seek=8 count=1` to clear the BROM boot region.
Open items (no leading "v" in the file):
- New FPGA programming needs ADC1/ADC2 offset calibration against each other (nothing connected -> same reading; FPGA corrects one).
- 1014D "reversal left of after sys_display_init".
- Display test code adjustable via USB (maybe based on CH340 implementation).
- Picture view bug: traces drawn with wrong offset in picture view (short time base settings; long ones are right).
- USB connect: some files show no size/data until refresh.
- Improve calibration (proper reference voltage per range and magnification).
- Battery charge display: draw offscreen then copy; more stable filter.
- Test code writing the sample buffer to SD per time base (100 waveforms), thumbnail buffer.
- A 4 GB SD card that works in the HDD bay does not work in the scope (check SD card types).
- Add USB mass-storage "prevent removal" handling (lock symbol); SCSI commands are sent but discarded; disconnect interrupt to return to scope mode.
- Settings save disabled during development (`scope_save_configuration_data()` call removed in power-down interrupt).
- Clean up #includes after variables.c/.h move; convert immediates to #defines (runstate, waveviewmode); test view code for missing .bmp/.wav; USB touch handling for refactoring sys files.
- Fonts must be const data (font_3 partly fixed; .bss vs .rodata problem for all-zero data).
- Find where channel1ypoints/channel2ypoints are cleared/filled.
- Thumbnail file numbers + file name display in picture/waveform view; SD last-sector access via USB SCSI hangs.
- Long time base: trace corrupted when a channel is re-enabled (line from top of screen, left artifact in top menu bar; trace pointer misalignment).
- Most-sensitive volt/div: trace jumps (offset handled in FPGA; compensation needed); ch2 differs from ch1.
- Test up-sampling; finish trace display; implement calibration, autoset, FFT, measurements display, protect flash settings with CRC, firmware update code.
- Display lib: separate screen/draw/source/destination buffer pointers; text alignment (left/center/right); star-like wide-trace idea.
- System-information menu item with version number; flicker: build trace screen in buffer and copy (also brightness slider); X-Y centering/direction; trigger-level offset when switching ch1 -> ch2; fill remaining settings on run/stop switch; check time base limit in single/normal trigger (50 ms/div).
- Done items ("v"): two-stage SD loader (bootloader at 0x2000 with fixed locations, second stage with FatFs), USB mass storage + delete hang fix, flash file packer (bitmap + check dwords for update), power-off detect for settings save, cursor measurements, settings/calibration save and load, battery monitoring, flash write, SD card code, X-Y trace pointers, notification confirmation optional, menu-open lock until touch released.
- Emulator note: with -O1 rounded rectangles break in the emulator (works on hardware, -O0 in emulator); original 1014D limits time base to 10 ms/div in single and 50 ms/div in normal trigger.
