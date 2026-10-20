# Web sources for FNIRSI 1013D / 1014D / DPOS350P (outside the 4 known sources)

Legend: [F] = fetched and resolved, facts read from page. [S] = seen only in search result (not fetched, or fetch blocked). 403 = fetch blocked but URL exists per search.
Excluded as already known: EEVblog 1013D thread, pecostm32/FNIRSI_1013D_Firmware, pecostm32/FNIRSI-1013D-1014D-Hack (readme fetched only for extra facts), Atlan4/Fnirsi1013D.

## 0. Key takeaways
- FPGA differs by unit/source: Anlogic EF2L45LG144B (CNX 1013D; EEVblog PCB dump of DPOX180H), "AL3-10" (pecostm32 bitstream RE; Atlan4 calls it AL3S10), Altera EP4CE6E22C8N (mysku 1013D teardown), "Cyclone IV" (Verstraten ADS1013D 2020). Hardware revisions exist; no source gives a clean ID rule. Identify by reading FPGA package marking. Hypothesis (unproven): early units Altera, later units Anlogic.
- ADC: 2x AD9288 (dual 8-bit, 40/80/100 MSPS grades), markings scratched. Evidence: "AD9288" strings in firmware dump (UniSoft on mysku). Real rate ~200 MSa/s per channel (aliasing test: 125 MHz shows as 75 MHz). 1014D thread: 2 ADCs per channel, FPGA cmd 0x29 selects one or both; both only at 10 ns/div.
- Real analog BW measured: ~20 MHz (Verstraten), ~25-30 MHz (CNX comments, beis.de ~30 MHz), ~36 MHz -3 dB with x10 probe (iXBT), 80+ MHz usable amplitude (mysku, optimistic). Rise time 10.5-21 ns.
- Small-signal suppression and undersampling at slow timebases are known flaws (beis.de, iXBT, CNX).
- DPOS350P: new architecture per developer "yangyao" (EEVblog), not 1013D-derived, FPGA reportedly Gowin (pcprogrammer), no public teardown with chip IDs found.
- Lots of MCP-for-scope prior art (Rigol/Siglent). Best design models: ikatkov/rigol-mcp, grayzhuang/oscilloscope-mcp, zinntikumugai/rigol-oscilloscope-mcp.

## 1. GitHub repos

### 1.1 Donwulff/FNIRSI_1014D_Firmware [F]
URL: https://github.com/Donwulff/FNIRSI_1014D_Firmware (branch `atlan4-base` active; `main` pristine upstream; `PORT_A` superseded 2023 port)
- Experimental open firmware for BOTH 1013D and 1014D from one tree, select with `PORT_1014D` in `fnirsi_101xd_scope/port_config.h` (default 1 = 1014D). GPLv3.
- Status per README (2026-08-21): 1014D variant runs on hardware (SD boot, clock init, traces, keys, rotary, menus, USB mass storage). 1013D variant compiles, never hardware-tested from this tree. 100-finding code review, two fix waves, most unverified on hardware.
- Bootloader recovery keys at power-on: F1 = SD firmware, F2 = original SPI-flash firmware, F3 = FEL mode.
- `fpga/` holds an Anlogic TD project retargeting Atlan4's AL3 replacement FPGA design to the stock 1014D board; built, never flashed. Loader accepts stock FPGA ID 0x1432 and custom 0x1532.
- Rebuilt roll/long-timebase mode on stock FPGA protocol.
- Build: Linux x86-64, `arm-none-eabi-gcc`, `make` in `fnirsi_101xd_scope/`. Flash: `dd` packed `fnirsi_1014d.bin` to SD raw device at 8 KiB offset (bs=1024 seek=8); needs MBR + FAT32 partition starting >= 1 MiB. Try in RAM with `sunxi-fel`. Settings/calibration moved to reserved sectors after 2026-09-20 (FEL migration needed).
- Docs inside repo (high value): `CLAUDE.md`, `AGENTS.md` (AI-agent context), `BOOT_NOTES.md` (boot chain), `FPGA_NOTES.md` (FPGA protocol and capture geometry), `PORT_AUDIT.md`, `ROADMAP.md`, `REVIEW-2026-08-21.md`, `PORT_A.md`. `tools/` has host-side analyzers for capture data.
- Lineage: pecostm32 1013D fw -> Donwulff quick 1014D PoC (Jan 2023) -> pecostm32 1014D fw -> Atlan4 evolved 1013D -> this merge.

### 1.2 pecostm32/FNIRSI_1014D_Firmware [F]
URL: https://github.com/pecostm32/FNIRSI_1014D_Firmware (GPL-3.0)
- 1014D only, "won't work on the 1013D". Dirs: `fnirsi_1014d_scope/`, `fnirsi_1014d_startup/` (boot loader + startup menu, added 28 Jun 2024; hold key at power-on to pick firmware).
- No FPGA/ADC text in README.

### 1.3 pecostm32/Anlogic_AL3-10_Analyzing [F]
URL: https://github.com/pecostm32/Anlogic_AL3-10_Analyzing (GPL-3.0)
- Bitstream RE of the AL3-10 FPGA in the 1013D; netlist also made for 1014D (`FNIRSI-1014D_FPGA` folder). Builds on mmicko/prjtang.
- Tools: bits_in_tile_lister, bitstream_analyzer, database_converter, flash_to_bit_converter (flash dump -> .bit usable in TD), netlist_tracer (gate-level Verilog), pythonProject (decodes TD IDE files).
- Format: unused bits at START of each frame; prjtang X/Y swapped; frame = 257 bytes (2056 bits), db uses 2052.
- Last log 19-11-2022: generated Verilog works partly (MCU comms, input relays, brightness work); sampling reset bit differs; readable version fails on 200 MHz timing; project paused.
- Companion EEVblog thread [F] https://www.eevblog.com/forum/fpga/reverse-engineering-anlogic-al3_10-fpga/ : TD IDE used for test designs; 1673 nets (1013D) and 1699 nets (1014D); LUT = 16 entries, two combine to a 5-input LUT; 15 BRAMs as 32x4096 simple dual-port; 50 MHz clock input on pin 23; goal .adc pin file + Verilog, unfinished. OP chose AL3_10 as simpler than the Gowin FPGA in Hantek DSO2000.
- 1013D thread page 54 [S] https://www.eevblog.com/forum/testgear/fnirsi-1013d-100mhz-tablet-oscilloscope/1325/ : pcprogrammer converted FPGA flash dump back to TD-usable .bit; ~51429 of 2205900 config bits to analyze.

### 1.4 roberttidey/FNISR1013DScope [F]
URL: https://github.com/roberttidey/FNISR1013DScope
- Decodes 1013D waveform `.wav` files: each capture is a 15,000-byte binary containing buffer data, display data, capture settings.
- `FNISR1013D-JSON.py` -> JSON of settings + buffer + screen (measures section still unmapped). `scopedump.vbs` exploratory dissector. `FileFormat.txt` has offsets (not read). Sample sets `DataVals.*`, `Measures.*`.
- 20 stars, 9 forks, 3 open issues.

### 1.5 Igor-kor/Fnirsi-Wav-Viewer [F]
URL: https://github.com/Igor-kor/Fnirsi-Wav-Viewer (MIT)
- Vue 3 + Vite web viewer for 1013D/1014D `.wav` captures; credits Tidey for format. Early stage (42 commits). TODO: measurements, FFT, XY.
- Also wavoigt/FNIRSI-1013D-WAV-Viewer-in-Excel-VBA https://github.com/wavoigt/FNIRSI-1013D-WAV-Viewer-in-Excel-VBA [S].

### 1.6 FNIRSI-1013D org [F]
URL: https://github.com/FNIRSI-1013D : repos `software` ("original software pulled from internal memory") and `hardware` ("parts, schematics, ..."), both last updated Jul 6 2020. Contents not inspected (possible stock firmware dumps / part lists / schematics).

### 1.7 pecostm32/FNIRSI-1013D-1014D-Hack readme.txt [F] (extra facts only)
URL: https://github.com/pecostm32/FNIRSI-1013D-1014D-Hack/blob/main/readme.txt
- Flash variant W25Q32 (32 Mbit) scope; region 0x001FD000 (0x1F4 bytes) = likely calibration, back up before flashing.
- Some units have GOODIX GT915(7) touch controller with reversed coordinates; fix = rewrite touch config; comms tuned ~200 kHz.
- Brightness controlled via a special IC through an FPGA parameter command. Relays click on sensitivity change.
- USB work emulated a CH340 as basis for mass storage. FatFs for SD. Power-off interrupt saves settings.
- FEL test files used with `sunxi-fel`.

### 1.8 Bare-metal / BSP for F1C100s
- minilogic/f1c_nonos [F] https://github.com/minilogic/f1c_nonos : bare-metal examples for F1C100S/F1C200S. Dirs `drv` (low-level drivers), `lib`, `src`, `tools` (sunxi loaders, zadig, iperf). Examples: demo, gui (LVGL7/9), tinygl, slideshow (SD+FatFs), `usbd` (USB device), `usbh` (USB host), twi, lwip over USB-Ethernet RTL8152B, audio, coremark. RAMSIZE 32M for F1C100S. No DMA or throughput figures documented; read `drv` and `src/usbd` for a hand-rolled MUSB device driver. License not stated on page.
- minilogic/f1c_dbc https://github.com/minilogic/f1c_dbc [S]: hobby computer on same SoC.
- nminaylov/F1C100s_projects [F] https://github.com/nminaylov/F1C100s_projects : GPL-3.0; drivers: CCU, interrupt ctrl, GPIO/EXTI, timers/WDT, PWM, UART, Display Engine (TCON, DEBE, DEFE), TVE/TVD. NO DMA/USB/SPI/SD drivers. Lichee Nano 800x480 LCD examples incl. LVGL. `tools/mksunxi` adds checksum to eGON.BT0 header. Boot via U-Boot (`fatload mmc 0:1 80000000 x.bin; go 80000000`) or simple_loader in SPI flash (loader at 0, app at 0x10000), JTAG with OpenOCD.
- hathach/tinyusb [F] https://github.com/hathach/tinyusb : F1C100s/F1C200s supported in DEVICE mode, high-speed marked supported, driver `sunxi`, note "musb variant", host unknown. BSP https://github.com/hathach/tinyusb/tree/master/hw/bsp/f1c100s [F] (Lichee Pi Nano, Widora Tiny200 v2/MangoPi-R3c). Uses `xfel`: `make BOARD=f1c100s exec` (load to DDR) / `flash` (SPI flash). TODO noted: F1C100s not yet in the example high-speed MCU check. CDC device class available (best path to USB CDC on bare metal).
- everlink/f1c100s-sunxi-tools https://github.com/everlink/f1c100s-sunxi-tools [S]: sunxi-tools fork for F1C100s.
- xboot/xfel [F] https://github.com/xboot/xfel : MIT, FEL tool with `spinor`/`spinand` read/write/erase, `ddr` init, `exec`, `read32/write32`, `sid`, `jtag`. Upstream sunxi-fel handles SPI NAND poorly (whycan reports https://whycan.com/t_5058.html [S]); NOR via `sunxi-fel -p spiflash-write 0 file.bin` works.
- DavidClawson/OpenScope-2C53T https://github.com/DavidClawson/OpenScope-2C53T [S]: RE/firmware for FNIRSI 2C53T scope/DMM/generator (different SoC); model for an open-firmware project.

## 2. Teardowns / reviews

### 2.1 CNX Software 1013D teardown [F]
URL: https://www.cnx-software.com/2022/11/16/fnirsi-1013d-teardown-and-mini-review-a-portable-oscilloscope-based-on-allwinner-cpu-anlogic-fgpa/
- FPGA ANLOGIC EF2L45LG144B (4480 LUT/DFF, ~35 kb dist RAM, ~700 kb BRAM, 15 M18x18 mult, 1 PLL). F1C100s QFN88 with 32 MB DDR in-package. 2 ADCs, markings illegible; AD9288 per EEVblog commenters.
- 7" 800x480 cap touch, 6000 mAh Li-ion, 1 GB storage.
- Commenters: BW ~25-30 MHz; ADCs 200 MSa/s each (not shared between channels); rate by timebase 200 MS/s (10 ns), 50 MS/s (1 us), 5 MS/s (10 us), 500 kS/s (100 us); rise time ~10 ns with 300 ps steps; no ext trigger, no FFT display (stock), no equivalent-time mode, no oversampling at slow timebases.

### 2.2 mysku.club 1013D teardown (Russian) [F]
URL: https://mysku.club/blog/aliexpress/80036.html
- FPGA Intel EP4CE6E22C8N (Cyclone IV E) + F1C100S. ADC worn; AD9288 strings found in firmware dump (UniSoft comment). SPI flash Winbond W25Q16JV; a SECOND SPI flash reportedly holds FPGA config. Possible I2C EEPROM near FPGA. 2x TP4056 in parallel. Full-size microSD (16 GB ok).
- Measured: usable amplitude to ~80+ MHz, drift from 85 MHz; ~200 MSa/s ADC; both channels same BW; trigger on one channel only; 10 mV/div visible with ~1 mV spikes; battery 6000 mAh (~5500 measured), 1.5 A charge draw.
- Input section schematic shared in comments (R12 10-22 ohm and C2 tweaks flatten response). Clone Utrnix ADS1013D has same hardware; flash dumped and reflashable in-circuit by holding controller in reset. One unit: flux residue near ADC caused ~2 V channel offset.
- Page was 110k chars; last 10k unread.

### 2.3 Verstraten ADS1013D test (Daniu/FNIRSI/KKmoon, 2020) [F]
URL: https://chinese-electronics-products-tested.blogspot.com/p/ads1013d-tablet-oscilloscope-tested.html
- Chips: TP4058 x2 (charge), SPX29302T5 (5 V/3 A LDO), AMS1117 (3.3 V), TPS61040 (boost, display supply), F1C100S, "Cyclone IV" (author's ID), AD9288 x2, W25Q16 (16 Mbit), BU4317G-TR x2 (Rohm voltage detectors) driving 2x3 relays.
- Measured: BW ~20 MHz (-3 dB; 5 MHz with 1/1 probe), rise ~21 ns (DSO5102P ref 3.9 ns), battery 6 h 40 min, no visible noise at 50 mV/div, FFT useless (no scales). 100 MHz at 10 ns/div shows 13 periods.

### 2.4 beis.de "Hands off the FNIRSI 1013D" [F]
URL: https://www.beis.de/Elektronik/FNIRSI-1013D/FNIRSI-1013D_en.html
- ~30 MHz Y-BW (rise 10.5 ns). DC within +-0.7 div of zero line suppressed; AC below ~0.35 div (~35 mVpp) suppressed. 720 X samples per screen speculated. Random (repetitive) sampling only at fast timebases, builds up in ~1 s. Intermittent "corrupted X deflection". USB ground not at BNC ground -> Y-shift when PC connected. Aliasing at 2 ms/div with kHz signals.

### 2.5 iXBT Live 1013D review (Russian) [F]
URL: https://www.ixbt.com/live/instruments/obzor-dvuhkanalnogo-oscillografa-fnirsi-1013d-s-7-dyuymovym-sensornym-ekranom-igrushka-kotoraya-sovsem-ne-igrushka.html
- Alias test: 125 MHz displays as 75 MHz => 200 Msps. ~36 MHz -3 dB with x10 probe. Above ~43.3 MHz square input shows as clean sine at 10/25 ns/div. 8 bit. Firmware build stamp 22 Mar 2020. FPGA "probably Chinese Cyclone IV clone" (unconfirmed). Header photo shows newer PCB revision than the one torn down.

### 2.6 mysku 1014D review (Russian) [F]
URL: https://mysku.club/blog/misc/85585.html
- F1C100s, 32 MB DDR, 1 GB flash, two dual-channel ADCs, separate FPGA (unidentified), 8 bit. Gen: 14 waveforms, sine to 10 MHz. Software trigger, no ext trigger. Storage 240 Kbit. Comments: similar to Hantek DSO2D15; each ADC ~200 MSa/s.

### 2.7 Circuit Digest "FNIRSI-1014D teardown" [F] -- LOW TRUST
URL: https://circuitdigest.com/articles/fnirsi-1014d-oscilloscope-teardown
- Says F1C100s + EF2L45LG144 + GD32 MCU (names inconsistent: GD32E230 / E303 / F303), "embedded Linux", INR BOM table. Contradicts the RE work (no Linux). Treat as unreliable.

### 2.8 Zhihu teardown (Chinese) [S, 403]
URL: https://zhuanlan.zhihu.com/p/585897079 : "dual-channel tri-core domestic portable scope teardown". Could not fetch; retry via browser.

### 2.9 Other forum threads (Russian) [S]
- https://forum.qrz.ru/13-pribory-i-izmereniya/56560-ostsillograf-fnirsi-1014d.html : 1014D switching-PSU noise at low levels, power-bank suggestion (matches EEVblog 1014D RE thread).
- https://forum.cxem.net/topic/280908 (1013D repair; input uses KAQY214 relay and RS8752 comparator per reply), https://forum.cxem.net/topic/253262 (ch2 artifact), https://radiokot.ru/forum/viewtopic.php?f=10&t=159311&start=6780 (claims AD9228, likely typo of AD9288), https://zvukaudio.com/viewtopic.php?t=1513, https://www.drive2.ru/b/695917743932703119/.
- EEVblog "New bench scope - Fnirsi 1014D, 7", 1GSa/s" https://www.eevblog.com/forum/testgear/new-bench-scope-fnirsi-1014d-7-1gsas/ [S].

### 2.10 EEVblog "Reverse engineering the FNIRSI 1014D" [F]
URL: https://www.eevblog.com/forum/projects/reverse-engineering-the-fnirsi-1014d/ (pcprogrammer, May-Jun 2024)
- FPGA AL3-10, command-driven from MCU side. Cmds: 0x0D sample rate, 0x0E time/div (matches 1013D except 200 MSa/s entry), 0x18 trigger status, 0x29 ADC count (1 or 2 per channel; both only at 10 ns/div), 0x46 gen buffer data, 0x47 gen buffer length, 0x48 unknown 0/1.
- A special IC sets the sample clock (clock synthesizer, max ~200 MHz; arbitrary-frequency programming not solved). Likely MS5351M-class part (unconfirmed).
- Secondary MCU: single-byte request/response protocol for keys/encoders, documented by donwulff.
- Front-end ground lifted to ~1.25 V; USB to grounded PC shifts center ~-1.25 V. Stock PSU noise corrupts readings; battery pack cleaner.
- Gen buffer 1 KB, software caps 1000 samples; square max 2 MHz at 100 samples; sine max 10 MHz at 20 samples.
- SD boot: `dd bs=1024 seek=8` (sector 16); recover by clearing sector 16. FEL used for test loads; DRAM init works from FEL; a 4 GB card failed to boot while the 8 GB original works.
- Gotcha: arm-none-eabi-gcc `char` is unsigned, broke signed compares. Test build fnirsi_1014d_V_A0.001 (Jun 22 2024).

## 3. Datasheets / manuals (URLs)
- F1C100s Datasheet V1.0 (2015-11-10, 21 pp) [F, PDF resolves]: https://linux-sunxi.org/images/b/ba/F1C100s_Datasheet_V1.0.pdf . Per search snippet: USB OTG with descriptor-based internal DMA, 128-byte FIFO, Normal/Dedicated DMA. Text not extracted (compressed streams); run pdftotext locally.
- linux-sunxi F1C100s wiki https://linux-sunxi.org/F1C100s [S, fetch 403]: lists datasheet V1.0; no F1C100s user manual; points to F1C600 User Manual V1.0 (357 pp, 2015-11-10) "alleged identical to F1C100s"; also F1C200s User Manual V1.0 (352 pp, 2017-04-18) and F1C200s Datasheet V1.1 (may differ).
- thirtythreeforty https://thirtythreeforty.net/posts/trying-the-f1c200s/ [F]: F1C200s is drop-in for F1C100s (64 MB vs 32 MB DRAM); `sunxi-fel version` reports soc=00001663 (F1C100s) for both; mentions F1C200s User Manual v1.1 (register definitions); link is in the post's resources section.
- F1C800 Datasheet V1.0 https://linux-sunxi.org/images/4/49/F1C800_Datasheet_V1.0.pdf [F, PDF resolves] (related family).
- Anlogic EF2 series datasheet DS400 v4.5.9 (2023-06, Chinese "SALELF 2") https://atta.szlcsc.com/upload/public/pdf/source/20240719/FDFD16A107E56F8FD9F1824888F6618E.pdf [S]; EF2L45 comparison (4480 LUT/DFF, 35 Kb dist RAM, 684 Kb BRAM, 15 DSP, 16 global clocks, 1 PLL) https://dfsimg2.hqewimg.com/group5/M00/02/1B/wKhk3mLGWGaAXvpdABFFtOe8C7I754.pdf [S]. Elnec lists EF2L45 in LQFP144/ftBGA256 [S].
- Anlogic toolchain: Tang Dynasty (TD) IDE; Sipeed install guide tang.sipeed.com [S]; yosys techlibs/anlogic example uses TD_HOME (EG4S20). No source confirms official TD support for AL3S10/EF2L45 beyond use by pecostm32/Atlan4.
- AD9288 (dual 8-bit, 40/80/100 MSPS, 3 V, 90 mW/ch @100 MSPS, 1 Vpp input, 475 MHz analog BW, SNR 47 dB @41 MHz, LQFP-48): https://www-cce.cldnet.analog.com/media/en/technical-documentation/data-sheets/AD9288.pdf and https://www.alldatasheet.com/datasheet-pdf/pdf/48539/AD/AD9288.html [S].
- MS5351M (Ruimeng/RELMON Si5351 clone; I2C, 3 outputs, 2.5 kHz-200 MHz, MSOP-10): https://www.qrp-labs.com/images/synth/ms5351m.pdf [S] (V1.0 2021-03-08); LCSC C1509083. Presence on scope unconfirmed; 1014D thread's "clock synthesizer max ~200 MHz" is consistent.
- Altera EP4CE6 Cyclone IV E handbook: not fetched.

## 4. USB on F1C100s bare metal; FEL
- No measured MUSB throughput found anywhere. Silicon: USB OTG, high-speed, internal DMA, 128-byte FIFO (datasheet snippet). Reference points only: HS bulk theoretical ~60 MB/s; emUSB-Device claims ~42 MB/s on strong parts and ~5 MB/s on a 120 MHz Cortex-M (https://doc.segger.com/UM09001_emUSBD.html [S]). Must be measured on the scope.
- Practical stacks: TinyUSB sunxi driver (device, HS, CDC class); minilogic/f1c_nonos `src/usbd`; stock-derived fw exposes SD as MSC; Atlan4 has a CDC mode (known source).
- 1013D USB ground differs from BNC ground (beis.de); PC attach shifts Y offset.
- FEL entry: hold BOOT, tap RESET, release BOOT -> VID:PID 1f3a:efe8 (Lichee Nano guide; TinyUSB README). On the scopes, Donwulff bootloader F3 enters FEL; pecostm32 FEL test files load via sunxi-fel; DRAM init works from FEL.
- Tools: sunxi-fel (man page https://manpages.ubuntu.com/manpages/lunar/man1/sunxi-fel.1.html [S]), xfel (MIT).

## 5. FNIRSI DPOS350P
- Official https://www.fnirsi.com/products/dpos350p [F, title/headline only]: 350 MHz, 1 GSa/s (1 ch; ~500 MSa/s per ch with both per some reviews), 47 kpts (some listings 60k), 8-bit ADC with software "high-res" to 16-bit (averaging, lowers BW), 8000 mAh, 7" touch, digital phosphor 50k wfm/s, generator 0-50 MHz (1 Hz step, 0-5 Vpp, +-2.5 V offset, 14 waves, up to 500 custom), FRA 10 Hz-50 MHz (20-500 pts), spectrum analyzer 200 kHz-350 MHz FFT 4K-32K with 2D/3D waterfall (OneSDR https://www.onesdr.com/fnirsi-dpos350p-digital-oscilloscope-review-2/ [F]). Edge trigger only, no protocol decode, no external trigger, no digital trigger, no RTC (EEVblog p1).
- Collections page claims "FPGA+MCU+ADC architecture" (https://www.fnirsi.com/collections/tablet-oscilloscope [S]; its 250 MS/s / 50 MHz numbers conflict, likely generic copy).
- Developer thread [F] https://www.eevblog.com/forum/testgear/hello-everyone-i-am-the-developer-of-dpos350p-and-dpox180h/ (user yangyao, page 1 of 5): he did all SW/FPGA/HW; software is a NEW architecture unrelated to 1013D; DPOS350P is an upgraded HW+SW architecture based on DPOX180H; actual analog BW "around 400M" (1 Vpp, no 50-ohm termination); says past FNIRSI BW figures weren't -3 dB. Unanswered: max sample rate, OS, ext trigger.
- Main thread [F pp.1,9,13,17 partial] https://www.eevblog.com/forum/testgear/new-oscilloscope-from-fnirsi-dpos350p/ : pcprogrammer says FPGA switched to Gowin (from Anlogic/Altera), MCU more powerful than 1013D, "four dual ADCs"; shallow memory; settings not retained at power-off; FRA min start 100 Hz, phase erratic/inverted; generator DC mode ~7% high (1.0 V -> 1.08 V); firmware fix released, further updates unlikely (ptluis); one user measured within 1.5 dB 10-350 MHz, -3 dB at 410 MHz; USB-C needs QC adapter, runs on 12 V.
- DPOX180H sibling: Kerry Wong teardown http://www.kerrywong.com/2023/09/08/fnirsi-dpox180h-teardown-pictures/ [S; fetch refused]: F1C100s + 32 MB DDR, two ADCs with scratched marks interleaved at ~250 MSPS each, discrete front end with small op-amps + 74HC4051 mux per channel. EEVblog DPOX180H thread [F] https://www.eevblog.com/forum/testgear/new-toy()-scope-fnirsi-dpox180h-claimed-180mhz500msps-(may-2023)/ : PCB dump (tunk) shows F1C100s + Anlogic EF2L45LG144B + two unidentified ADCs, PCB similar to 1013D/1014D. Real BW ~60 MHz (RAPo).
- Videos [S, no transcript retrieved]: https://www.youtube.com/watch?v=nILiuOlv9V8 (DPOS350P review/teardown), https://www.youtube.com/watch?v=GshPGxCVHto (Uni-Byte 0333 DPOS350P from top to bottom), https://www.youtube.com/watch?v=9-EHjiijSA0 (DPOX180H teardown). Need manual viewing for chip IDs.
- Manuals: https://manuals.plus/asin/B0F6C96GWW [S]; https://fnirsioscilloscopes.store/manuals/fnirsi-dpos350p-english-manual/ [S, wrong data: battery/LA claims conflict]. andibond review https://www.andibond.com/fnirsi-dpos350p-4-in-1/ (403). AliExpress "firmware guide" is SEO junk.
- No source found on DPOS350P firmware file format or SoC. The Gowin FPGA claim (pcprogrammer) is the only silicon hint.

## 6. MCP / SCPI agent prior art (design models)
- ikatkov/rigol-mcp [F] https://github.com/ikatkov/rigol-mcp : raw TCP SCPI (port 5555), no VISA. Tools: scope_info, get_screenshot, get_stats, get_waveform (stats + CSV file), plot_waveform (PNG), run_control, set_channel/timebase/trigger/acquire, scpi escape hatch, search_manual/get_manual (offline vendor manual search). Stateless connection per call; chunked deep-memory reads (250k/read); side effects documented per tool; RAW reads stop the scope; actionable setup errors.
- grayzhuang/oscilloscope-mcp [F] https://github.com/grayzhuang/oscilloscope-mcp : plug-in `Scope` ABC per vendor, capability YAML profiles (BW, sample rate vs channel count, memory depth, idn regex), `caveats[]` attached to every output, validate before sending, `scope_waveform` returns Schmitt-quantized run/edge stream, `scope_compare` sim-vs-measured edge diff, `scope_viewer` HTML viewer. Best template for a capability-profile approach (fits 1013D's odd BW/sample-rate caveats).
- zinntikumugai/rigol-oscilloscope-mcp [F] https://github.com/zinntikumugai/rigol-oscilloscope-mcp : 28 tools, 4-tier safety READ_ONLY/SAFE_WRITE/RESTRICTED_WRITE/DANGEROUS_WRITE, two-step confirmation token bound to args + settings snapshot, JSONL audit log, read-back of applied values, fail-closed UNSUPPORTED_FEATURE, host-side FFT, serial decode, 100k point default cap, screenshots PNG/JPG/BMP/WebP.
- MagnusJohansson/siglent-sds-mcp [F] https://github.com/MagnusJohansson/siglent-sds-mcp : SCPI over TCP 5025, 12 tools, command queue serializes I/O, non-blocking auto-connect, IEEE-488.2 block parsing, raw codes -> volts, `CHDR OFF`, inline screenshot.
- Others [S]: lpender3672/mcp-hardware-tools (DS1054Z, pyvisa-py), erebusnz/rigol-mcp, gloveboxes/rigol-mcp (restricted by default; RIGOL_ENABLE_SEND_RAW), krystalmonolith/skippy-mcp (glama.ai), abhinav937/pyvisa-mcp, techmanual-ai/lablink-mcp.
- Python on FNIRSI USB (other products): PyPI fnirsi-dps150, https://github.com/baryluk/fnirsi-usb-power-data-logger as templates for vendor-USB drivers. No PC-side app or documented CDC protocol for the 1013D found outside the Atlan4 repo.

## 7. Gaps
- No Hackaday, Reddit or GitLab hits on these scopes.
- No public 1013D/1014D schematic (cxem threads ask for one); FNIRSI-1013D org `hardware` repo not inspected.
- No F1C100s MUSB throughput numbers; no official F1C100s user manual (use F1C600 V1.0 / F1C200s V1.0-1.1).
- No DPOS350P internals with chip numbers.
- Blocked: linux-sunxi.org, dl.linux-sunxi.org, kerrywong.com (connection refused), zhihu, andibond, patreon.
