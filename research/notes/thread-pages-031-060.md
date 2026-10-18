# EEVblog FNIRSI-1013D thread, pages 31-60 (posts #750-#1499, Apr 2021 - Dec 2022)

Citation format: [pNN, poster]. Page N = offset (N-1)*25; post numbers (#) given where useful.
Source extraction was done by a summarising fetch tool, so numbers are second-hand. Verify critical values (command IDs, addresses) against the GitHub repos before relying on them.
pcprogrammer = Peter (NL), author of the open-source firmware and the reverse engineering. Atlan did not post in this slice (only thanked posts, p49 and p51).

## 1. Repositories and external links (exact)

| What | URL |
|---|---|
| Main hack repo (Ghidra archives, schematics, test code, binaries, FEL tools) | https://github.com/pecostm32/FNIRSI-1013D-Hack |
| Same repo renamed later | https://github.com/pecostm32/FNIRSI-1013D-1014D-Hack [p57, p58, p60] |
| Open firmware repo (bootloader + startup + scope projects, dist/ binaries) | https://github.com/pecostm32/FNIRSI_1013D_Firmware [p34 sig onward, p53 #1303] |
| Prebuilt scope binary | https://github.com/pecostm32/FNIRSI_1013D_Firmware/blob/main/fnirsi_1013d_scope/dist/Debug/GNU_ARM-Linux/fnirsi_1013d.bin [p58] |
| Config file format doc | https://github.com/pecostm32/FNIRSI_1013D_Firmware/blob/main/configuration_file.txt [p58, p59] |
| FPGA RE (Anlogic AL3-10) | https://github.com/pecostm32/Anlogic_AL3-10_Analyzing |
| Hantek DSO2000 schematics (author's other RE) | https://github.com/pecostm32/Hantek_DSO2000 |
| Lichee Nano + FA201 FPGA test setup | https://github.com/pecostm32/Lichee_Nano [p53, p56] |
| Analog input schematic image | https://github.com/pecostm32/FNIRSI-1013D-Hack/blob/main/Schematics/Scope_Analog_Input.png [p50 #1239, p60] |
| Schematics dir | https://github.com/pecostm32/FNIRSI-1013D-Hack/tree/main/Schematics [p37] |
| Bootloader code | .../FNIRSI-1013D-Hack/tree/main/Test%20code/fnirsi_1013d_bootloader [p31] |
| Scope code (early) | .../Test%20code/fnirsi_1013d_scope [p36, p37] |
| FEL bootloader | .../Test%20code/fnirsi_1013d_startup_with_fel [p37] |
| Flash file packer (update image generator) | .../Test%20code/flashfilepacker [p43] |
| I2C bin converter (Saleae) | .../Test%20code/i2c_bin_data_converter [p34] |
| FPGA explained / FPGA_functions | .../FNIRSI-1013D-Hack/tree/main/FPGA%20explained and .../Test%20code/FPGA_functions [p35] |
| Display lib test | .../Test%20code/Display_Lib_Test [p36] |
| Software RE (Ghidra) | .../FNIRSI-1013D-Hack/tree/main/Software%20reverse%20engineering [p40, p50] |
| sunxi helper files | .../FNIRSI-1013D-Hack/tree/main/sunxi_stuff [p43] |
| Linux on scope | .../FNIRSI-1013D-Hack/tree/main/Linux [p37] |
| Original dumps | .../FNIRSI-1013D-Hack/tree/main/Binaries/Original%20files (W25Q16cn.bin, W25Q16en.bin) [p31, vzgherea] |
| Backup tool source | FNIRSI-1013D-1014D-Hack, `Test code/fnirsi_1013d_firmware_backup` [p57] |
| Early board photo (Altera marking) | github.com/pecostm32/FNIRSI-1013D-Hack/blob/main/Pictures/Old%20version/Main_board_old_system_large.jpg [p47] |
| u-boot port for 1013D (iscle) | https://github.com/Iscle/u-boot-1013d (board.c L91 patch) [p35, iscle] |
| 1013D emulator (author) | emulator in FNIRSI-1013D-Hack (Xlib, Linux) [p32-p37] |
| QEMU F1C100s + 1013D machine (cool2000) | https://github.com/froloffw7/qemu ; https://github.com/froloffw7/FNIRSI-1013D-1014D-Hack ; https://github.com/froloffw7/Qemu-FNIRSI-1013D-Emulation-Screenshots [p58] |
| WAV-to-JSON tool | https://github.com/roberttidey/FNISR1013DScope [p58, MilesStarr] |
| Anlogic RE thread | https://www.eevblog.com/forum/fpga/reverse-engineering-anlogic-al3_10-fpga/ [p58, p59] |
| prjtang (Anlogic bitstream format) | https://github.com/mmicko/prjtang (bitstream_format.rst) [p51, p54] |
| Anlogic TD IDE | dl.sipeed.com/shareURL/TANG/Premier/IDE (TD_5.0.3_28716_NL_Linux.zip) [p51] |
| Anlogic JTAG programmer fw | https://github.com/AnlogicInfo/anlogic-usbjtag [p51] |
| openFPGALoader (Icenowy fork) | https://github.com/Icenowy/openFPGALoader [p50, _AW] |
| AL3A10BG256 datasheet | https://www.semiee.com/file/Source10/Anlogic-AL3A10BG256.pdf [p50] |
| AL3S10LG144 datasheet v1.6 | https://datasheet.lcsc.com/szlcsc/An-Road-Shanghai-Info-Tech-AL3S10LG144_C183192.pdf [p50] |
| AL3S10 datasheet v2.0 CN | http://www.anlogic.com/Private/Files/20190611135026194%E2%88%AEDS101_AL3S10_Datasheet_CN.pdf [p50] |
| Review "Hands off the Fnirsi 1013D" | https://www.beis.de/Elektronik/FNIRSI-1013D/FNIRSI-1013D_en.html (de variant too) [p35, p36, p38] |
| ixbt review (43.3 MHz fabrication) | https://www.ixbt.com/live/instruments/obzor-dvuhkanalnogo-oscillografa-fnirsi-1013d-s-7-dyuymovym-sensornym-ekranom-igrushka-kotoraya-sovsem-ne-igrushka.html [p46] |
| Saleae binary export format | https://support.saleae.com/faq/technical-faq/binary-export-format-logic-2 [p34] |
| FatFs | http://elm-chan.org/fsw/ff/00index_e.html [p40] |
| sunxi bootable SD | https://linux-sunxi.org/Bootable_SD_card [p45] |
| Picotech analog-offset note | https://www.picotech.com/library/application-note/using-analog-offset-to-maximize-oscilloscope-resolution [p50] |
| Chocolate fireguard thread (failure reports) | https://www.eevblog.com/forum/testgear/fnirsi-1013d-as-much-use-as-a-chocolate-fireguard/ [p60] |
| 1014D thread | https://www.eevblog.com/forum/testgear/new-bench-scope-fnirsi-1014d-7-1gsas/msg3577705/#msg3577705 [p43] |
| Key forum posts | firmware backup tool msg3807689; SD/touch config msg3808349, msg3809966; upgrade msg3908555, msg3917249; older firmware names msg3144296 |
| Backup demo video | https://youtu.be/h-tdKPDocHI [p45, p56] |

Binaries posted as .txt attachments on forum: fnirsi_1013d_fwb.bin (backup tool, p45), fnirsi_1013d_v0.002.bin (p52), v0.003 (p55), v0.004/v0.005 (p56), standard/shifted display variants (p50), NO_LABEL.7z Rufus 16 GB SD image (p60, later added to repo [p60 #1490]).

## 2. Hardware

### 2.1 SoC and memory
- Allwinner F1C100s (ARM926, ARMv5TE), sunxi-fel reports soc=00001663, scratchpad 0x7E00; USB FEL ID `1f3a:efe8 Onda (unverified) V972 tablet in flashing mode` [p31 #752, p43, p54].
- Chip is F1C100s with 32 MB DDR, not F1C200s (64 MB) [p39 #970, pcprogrammer]. DavidAlfa initially assumed F1C200s/64 MB [p39 #969].
- DDR is 16-bit; default 156 MHz (312 MHz effective), CPU default 408 MHz, ~600-768 MHz workable, DDR limit ~192 MHz; RAM has its own PLL [p39 #969, DavidAlfa; p40]. pcprogrammer's code: CPU 600 MHz, AHB 300 MHz, APB 150 MHz, caches on [p38 #931, p40]. Later firmware text says DRAM 156 MHz [p38 #930 question, unverified].
- DavidAlfa benchmark (CPU 768 MHz, TCON stopped), `dd` memcpy: 312 MHz DDR 273.88 MB/s; 408 MHz DDR 353.86 MB/s; scales linearly, so RAM is the bottleneck; theoretical 16-bit 624/816 MB/s, efficiency 86-88% [p39 #974, p40].
- ~72 KB SRAM [p39 #968]; BROM has 32 KB SRAM [p54]. DDR is shared with LCD scan-out, limiting speed [p39 #968].
- Stock firmware runs from DRAM; 1.6 MB original image, ~600 KB without bitmaps [p41, p44]. Stock refresh about 5 fps (DavidAlfa estimate) [p38 #930].
- SoC dissipation pad under chip; one user replaced a dead F1C100S (~US$3 on AliExpress) and unit revived but with jitter [p53 #1324, p54, p55 #1357].
- Same MCU as Lichee Nano [p43].

### 2.2 FPGA (variants across board revisions)
- Early units: Altera Cyclone IV EP4CE6E22C8N visible in board photo [p47, pcprogrammer]. JTAG IDCODE 0x020F1-style in tv84's bitstream header matches EP4CE6 [p49 #1207, morris6/tv84]. A genuine EP4CE6 shares die/IDCODE with EP4CE10 so may run CE10 bitstreams, not guaranteed [p49 #1212, voltsandjolts].
- Later units: Anlogic AL3A10LG144C7 (AL3-10, 144-pin), JTAG IDCODE 0x18006C31 (TAP "sesame" in OpenOCD, mfg 0x618) [p49 #1210, p50 #1248, p51 #1250-1254]. Board flash bitstream 282,866 bytes vs 368,011 bytes for EP4CE6 [p50 #1243, morris6]. Pinout is Cyclone-IV-compatible (designed as drop-in) [p51].
- Anlogic part has ~270 Kbit block RAM (earlier spec "240 Kbit storage depth", Valentin quote) [p33 #806/807, p39 #968, p48]. 4K x 32-bit memory organisation [p59].
- Config bitstream: 1075 frames x 257 bytes (2056 bits, 2052 used, unused bits at start of each frame), tile grid 0-34 x 0-37 [p51, p54]. 2,205,900 config bits [p54].
- FPGA config flash: Zbit ZB25VQ80 (1024 kB) on the board morris6 inspected [p49 #1207]; 4-pin header next to it [p33 #816].
- JTAG: 10-pin header on older boards routed to unpopulated pads [p55 #1369]; author wired a JTAG header mod [p54 #1336]. Reprogramming FPGA needs opening the scope [p48].
- Hantek DSO2D15 FPGA mentioned as future RE target [p58 #1436].
- A user reported crashes fixed by reflowing the FPGA (flux + IPA) on an early unit [p46 #1149, jemangedeslolos].
- Clone suspicion (p47/p49) was dropped when bitstream sizes differed (different FPGA, not clone) [p50 #1243].
- Rewritten FPGA Verilog "works on the scope" with the author's firmware, crude, possible timing errors; Original_1013D_fpga.zip (37.9 kB) [p59 #1451, Nov 10 2022].
- morris6 built his own FPGA design: works but trigger problematic [p55 #1353].
- Desired: 8 MB sample memory (AL3-10S has it, poor availability); Cyclone IV alternative has more DSP tiles, same memory [p55 #1353].

### 2.3 ADC and sampling
- 8-bit samples [p37, p41, p46, p50].
- Two ADCs per channel (4 total), interleaved in FPGA/CPU for the two fastest ranges; ADC clock 100 MHz each giving 200 MSa/s per channel real-time (5 ns/sample); box marketed as 1 GSa/s [p46 #1133, p47, p48, p50 #1229, p52 #1275].
  - Contradiction: p52 summary says "4 ADCs (one per channel path)"; p49-p50 say 2 per channel with mismatch; p46 says "two ADCs, FPGA returns samples from both".
- Original firmware: 10 and 25 ns/div use sample period 5 ns, 50-500 ns/div use 10 ns [p46]. Original FPGA cannot interleave; CPU does it (reads two 1500-sample buffers, ADC1/ADC2) [p42, p48].
- ADC calibration: offset difference between ADC1 and ADC2 per channel; original firmware compensates only when ADC2 < ADC1; example compensation 7 scaled to 12 at current V/div [p49 #1224]. Positive compensation causes ripple on bottom of trace (ADC1=0, ADC2=0+7 alternating) [p50 #1229]. Fix: clip below calibration value; keep trace offset in software [p49-p50].
- Measured ADC difference per unit [p51 #1264-1274]: pcprogrammer ch1 ~2 (1-3), ch2 up to 11; ded ch1 1..3, ch2 -5..-7; _AW ch1 -1..-3, ch2 6..13; Alex62 ch1 3..6, ch2 6..8. Differences vary with DC offset (none at offset limits 200/1500) and V/div (~1 bit).
- Calibration file in original: flag (0 subtract, 1 add) + value, 2 per channel, stored at 0x802F18CC/0x802F18CE [p42].
- Sample ADC clock for long timebases fixed at 50 KSa/s (measured with Rigol); at 200 KSa/s the ADC clock is 100 kHz; no FPGA anti-alias DSP [p50 #1231, #1240].
- Original lowest sample rate 2 kHz; one buffer ~0.75 s; 200 ms/div needs ~2.8 s [p47]. Author later added 1 kSa/s and 500 Sa/s via command 0x0D [p51 #1260].
- Buffer depth: 750/1500 samples original (1500 at 50 ms, 20 ms; "750 or 1500 bytes" reads) [p34, p40, p52]. Test firmware uses 3000 samples/channel at all timebases (FPGA RAM ~270 Kbit, only ~3000 used) [p48, p52, p56 #1383]. Original short timebase: 2500 samples normally, 1500 when timebase index < 25 [p40].
- Real-world: ~30 MHz bandwidth vs "100 MHz" label (author's estimate) [p59 #1454]; ~40 MHz usable limited by 200 MSa/s [p49 #1204]; good performance to ~10 MHz per geoffCohen [p47]; bianchifan tested to 25 MHz [p33].
- Stock firmware fabricates signal above ~43.3 MHz at 10/25 ns/div: a 43.3 to 43.4 MHz step turns any input into a pure sine; amplitude faked at 80 MHz [p46 #1135/1134, ixbt review]. Detection: >320 rising-edge counts (~43 MHz) selects different routine [p43].
- Sensitivity: low-signal trigger problems: CH1 shows nothing below 30 mV, trigger fires 29 mV, stable above 33 mV; CH2 junk 20-50 mV, trigger unstable up to 200 mV [p33 #820, bianchifan]. Trigger problem below ~200 mV "needs new FPGA implementation" [p35 #866].
- Calibrator: 1 kHz square, 3.17 Vpp expected, reads 3.17/31.7/317 Vpp for x1/x10/x100 [p38 #938]. geoffCohen: reads 3.15 V on 3.00 V reference (5% high), x10 probes ~10% error; proposes software gain correction [p47].
- Resolution: step ~3 mV at 100 mV/div over 8 divisions [p53 #1314].
- Idea: usable bandwidth ~40 MHz; equivalent-time sampling to 1 GSa/s needs five 1 ns phase shifts, repetitive signals only [p48 #1191].

### 2.4 Analog front end
- Relays: KAQY214STLD (solid-state relay) used for AC/DC switching, FPGA outputs ANALOG1_AC_DC / ANALOG2_AC_DC; K1/K6 switch op-amp stage between inverter and inverting amplifier; K2/K3 select input resistor combinations [p58 #1443/1445, pcprogrammer]. Relay clicks audible in sensitivity changes [p38].
- Op-amps: "RS8751RF" per marauder [p58 #1442]; CH1 replaced with OPA356, which fixed a ~1 V low offset [p58 #1447]. Op-amps run on 2.5 V supply [p55 #1371].
- Virtual ground VRF1 ≈ 1.25 V (measured 1.233 V, TL432-class reference) added back before op-amp; AC coupling caps C99 (CH1) and C133 (CH2); in DC mode caps shorted [p55 #1371, p56 #1376]. Gustavo's unit had shorted BNC decoupling caps (150 nF measured 0 ohm) [p56 #1379].
- Battery reference VREF: TL431 + 10 K, referenced to VBAT not GND (≈ VBAT - 2.5 V) [p37 #920, p55 #1373, Kean]. MCU samples it for battery status. Both measured ~1.19-1.23 V [p55 #1371].
- Trace offset: PWM (25 kHz) generated by FPGA drives ADC negative input (nets ADC1_OFFSET/ADC2_OFFSET after smoothing filters), applied after the amplifier, so it does not prevent front-end saturation [p50 #1231, #1238; p58 #1443].
- Input readings per V/div (ANALOG1 ADC input / relay state): 50 mV off 0.67 V; 100 mV off 0.67; 200 mV on 0.95; 500 mV off 0.99; 1 V on 1.11; 2.5 V off 1.17; 5 V on 1.20 [p58 #1442].
- 50 mV/div is software zoom: only 6 hardware ranges [p52 #1275, p35 #856]. Hardware ranges 500 mV/1 V share setting; scaling in software [p35].
- 10 MΩ input path; geoffCohen added 10 MΩ across BNCs as workaround [p47].
- Input LPF ~20 MHz per thread; capacitor mod would not reduce FPGA artifacts; suggested recomputing R/C for gain/bandwidth [p49 #1204, p53 #1321].
- External preamp ideas: OPA355 x10 (~15 MHz BW), transistor x10/x100 (~5 MHz) [p49 #1205, btidey].
- Evi offset mods for the 1.25 V bias (cut grounds at op-amps, -2.5 V supply idea) with photos Mod1/Mod2/FinalMod; pcprogrammer: whole front end would need replacing [p60 #1482-#1487].
- Design critique: no separate ground planes, weak ferrite filtering, linear regulators for main rails, boost converters only for display; ~15% ripple on negative rails [p57 #1411/1412].

### 2.5 Display, touch, backlight
- Display 800x480 RGB (2 bytes/pixel, 768000 B frame); 7-inch [p33 #804, p46]. Original firmware configures GT911 touch for 1024x600 and scales to 800x480; some replacement panels report 1024x600 [p31, p32, p35].
- Display variants: at least three types; one produces up-left image shift (fixed with display config sector); different LCD cable IDs [p55 #1369, p44].
- TCON0_BASIC_TIMING1 differs between bootloader (0x41E0044) and main (0x041E004A) [p32 #776]. DEBE framebuffer at 0x803849A0; video buffer 0x804FB9B4 [p32].
- Touch controller Goodix GT911 (some posts say GT915; the sketch name `I2C_GT915_reversal_command`, treated as typo) [p31 #761, p32 #779]. Config registers 0x8047 (version, must be >= previous), checksum, X/Y resolution 0x8048-0x804B, Y reversal 0x80B7-0x80C4, X reversal 0x80D5-0x80EE [p31, p32]. Touch status register 0x8150 [p33].
- Touch panel variants: common 10 sense x 16 drive (sens0-9, drv0-6 + drv15-23); other units 12 sense x 21 drive (sens0-11, drv0-10, drv12-21); also seen 20x14 [p43, p44, p55 #1369]. Large flex to LCD has no IC; touch is on small separate 6-pin 0.5 mm connector [p44, p46].
- Touch I2C is bit-banged by MCU on port A (pins 63/64/65 touch lines idle high 3.1 V); stock clock ~40 kHz, new firmware ~140 kHz or faster [p36 #892, p53 #1322-1323]. Interrupt line unused [p54].
- Brightness: PWM from FPGA via special IC; formula (brightness*560)+4000 [p50 #1246]; bootloader writes 1 byte [p46]. Backlight PWM ~800 Hz, boost soft-start requires <1 kHz; low brightness gives audible beep on one unit [p41, p57].
- Touch quirk: stock firmware freezes display while touched [p47]; touch timing delay loop removed by optimizer at -O1, replaced by asm loop [p38 #936].

### 2.6 Flash, SD, USB, power, misc
- SPI flash W25Q16 (2 MB; older "CN/EN" units 4 MB per one post [p43 #1064 says new unit 2 MB, original 4 MB]); also 25Q16 / 32 Mbit variants [p31 #766, p56 #1388]. Hardware check expects 0x9086 at flash 0x001FE000; settings/config block at 0x001FD000 (0x1F4 bytes, possibly calibration) [p31 #773-774, p42].
- Flash read bug on newer scope: first byte reads 0 due to SPI clock edge, fixed with slower SPI clock; 1013D code AHB/4 vs 1014D AHB/12, p46 mentions AHB/6 [p46].
- SD: microSD for screenshots and waveforms; 128 MB cards work for FEL; 4 GB and larger failed in some cases; 1/2 GB must be FAT16 with partition start >= sector 800 (2048 recommended); 16 GB FAT32 works; sdhc cards ok; card 32-bit FIFO needs aligned buffers (author rewrote for any alignment) [p40, p41, p42, p45, p48].
- SD driver based on u-boot sunxi-mmc; FatFs (elm-chan) in stock fw; stock file names: piclist.sys, wavelist.sys, pic_system.sys, wave_system.sys, 1.bmp-999.bmp, 1.wav-999.wav [p41].
- USB: Mentor MUSBMHDRC dual-role controller in F1C100s; stock uses STM32 VID for MSC; descriptors include unused HID/CDC/UVC [p44]. Stock firmware hangs on USB disconnect; author's USB MSC works, hang fixed [p45, p56].
- Power: MIC29302 linear 3.3 V regulator from BAT (failure: 1.92 V output, unit dead) [p60 #1498]; battery charge indicated by pin 0xC low; KEYADC 6-bit battery reading (full ~50, charging 56-58) [p36 #893, p37]. Current ~800 mA at 4.1 V, >1 A inrush [p53 #1324]. No battery undervoltage cutoff observed (cell hit 0 V) [p55 #1359]. Battery test quote: 6h45m runtime, 3.5 h recharge, 1.8 A to 0.3 A [p52 #1285].
- Test points / debug: UART0 init at 115200 in stock fw but no UART on hardware [p37 #908]. FEL via SD boot image (see 4.1). JTAG header on FPGA only.
- Hardware revisions: older "protruding BNC" units vs newer recessed-connector flat-glass units [p38 #948, p55 #1369]; Daniu ADS1013D, ADS1013D II [p57, p58]; FNIRSI-1014D uses GD32E230 and shares much with 1013D code [p39 #951, p57].
- Boot images: Allwinner BROM expects eGON.BT0 header at SD sector 16 (byte offset 0x2000... author quotes 8 KB offset via `dd seek=8` with bs=1024) [p36 #879-880, p45].

## 3. FPGA-to-CPU interface (as reverse engineered)

- The FPGA is reached through an intermediate "special IC" ("parameter IC", translation chip) over a custom 2-wire protocol that looks like I2C but is not [p32-p34, p35]. Details [p33 #815-816, p34 #826]: 820 kHz clock; 8-byte packets, 9 clock pulses/byte; frame has checksum and "crypt" check; key byte changes after each write/read; after write bit pairs swapped (7,2),(5,0),(6,4),(3,1); after read byte inverted; packets vary 7-11 bytes [p34 #847].
- Later conclusion: special IC is a command translator, not a store [p35 #854, p48]. Examples: ID 0x10 maps 8-bit brightness to 16-bit for FPGA cmd 0x38; ID 0x11 maps 0x14 reads (shift right 4 bits, add 2) [p34 #846]; ID 0x12 calibration (subtract two inputs, multiply by 100, intermittent zeros) [p50 #1241-1242]; ID 0x0B/0x0C/0x0D/0x0E channel/trigger related; 0x0C returns 0x20 for CH1, 0x0D returns 0x22 for CH2; CH1 requires 0x0C with trigger channel ID first or no valid samples [p48]. Responses differ per input: input 0 -> 0xAD, input 6 -> 0xB8 [p34 #845].
- The special IC returns zeros randomly and corrupts samples; author removed it entirely: direct FPGA command access works, V/div translation removed, traces stable [p50 #1246, p56 #1383, p55 #1358]. Parameter 0x11 (sample readout / trigger point) still used translation in test (range unknown) [p50 #1246].
- Initialisation IDs seen first: 0x0A, 0x14, 0x16, 0x10; then 0x11, 0x0C, 0x0B repeating; sometimes 0x0E, 0x0D [p34 #826, #835].
- Analog front-end commands [p35 #856]:
  - CH1: 0x32 offset, 0x33 V/div, 0x34 coupling (0 AC, 1 DC). CH2: 0x35 offset, 0x36 V/div, 0x37 coupling.
  - V/div codes with default offsets: CH1 1V 0x05/0x02E0; 2V 0x04/0x02E7; 5V 0x03/0x02D3; 10V 0x02/0x02DA; 25V 0x01/0x02B9; 50V 0x00/0x02B9. CH2: 0x05/0x0499; 0x04/0x049F; 0x03/0x0496; 0x02/0x049C; 0x01/0x048B; 0x00/0x048D. (500 mV uses the 1 V setting.)
- Channel enable: cmd 0x02 CH1, 0x03 CH2 [p37 #900].
- Sample read: FPGA cmd 0x20 (CH1), 0x21 (CH2) for fast timebases; 0x20-0x23 read sequence; 0x24/0x26 for 100 ms-50 s/div; 10 bytes per read at long timebases; two buffers per channel at 25/10 ns/div; one at 50 ns-50 ms [p34 #849, p35 #861, #863]. Reads 750 or 1500 bytes depending on timebase.
- Trigger: cmd 0x17 trigger level (1 byte); trigger time register 0x0E for <= 50 ms/div (values e.g. 10-500 ns: 00 06 45 DC; 10 ms: 00 00 07 08; 20/50 ms: 00 00 03 20), 0x0D for 100 ms-50 s (00 00 07 D0) [p35 #863]. Later: cmd 0x0D sets sample clock divisor (long timebase 2000 = 50 KSa/s; others 99999999, 39999999, 19999999 ... 99, 39, 19, 9, 3, 1, 0; 0 = 200 MSa/s) [p50 #1246]. No trigger position command; MCU handles horizontal trigger position, assumed mid-buffer (FPGA arms comparator after storing half capture, morris6 theory) [p48].
- Status/handshake: cmd 0x0A polled before sample read = wait-for-trigger (touch can interrupt it); cmd 0x05 polled twice (purpose unknown, maybe reset/clear); cmd 0x0F is 0 before capture and 1 after, calibration writes it 1 (suspect trigger-logic enable); cmd 0x1F needs 16-bit data before sample reads (suspected trigger-point select; fixed value prevents trigger) [p49 #1218, p50 #1230, #1244].
- Brightness/backlight cmd 0x38 [p35]; reading touch via cmd 0x41 [p33]. Trigger mode normal/single written to FPGA [p49 #1222]. A "disable trigger and acquire one buffer" command did not work in NORMAL/SINGLE, auto-set froze until trigger forced to AUTO [p56 #1393].
- Trace offset applied inside FPGA (PWM) shifts result; MCU does the rest [p47]. Calibration needs ADC1/ADC2 diff at DC offset 200 [p51].
- Software trigger/FPGA split: trigger partly software, partly FPGA; stock fw places trigger at mid-buffer; many wobble/jitter issues at high freq are FPGA-related [p35 #860, p52 #1296-1297].
- FPGA limitations found: no true segmented/pre-trigger control, 3000 samples/channel usable, interleaving not in FPGA, ~43.3 MHz detection, jitter at >1 MHz trigger offset [p52], capture errors across 1050 captures (empty buffer, fewer samples at 100 µs/div, different rate at 5 ms/div) [p47].
- Reverse-engineered data path from Saleae/DSLogic captures (CSV/bin conversion in repo); FPGA functions documented in repo "FPGA explained" folder and file "Startup and signal data read sequence.txt" covering 50 ns-50 ms [p35].
- CPU side: MCU-FPGA link uses a UART pin group GPIO bit-banged (not UART peripheral) [p40].

## 4. Firmware

### 4.1 Stock firmware layout and update mechanism
- SPI flash map: updater at 0x6000, startup image 0x13000, main program 0x27000, settings 0x1FD000, check word at 0x1FE000 (0x9086) [p31, p43]. Written in 0xFFFF-byte blocks, 30 blocks; read-back check; on error it rewrites first 1000 bytes of main program (zeroing / "system file damaged!!!") [p40, p43]. Code at init also clears 0x3E8 bytes and writes flash at 0x27000 (bootloader update logic) [p38 #941].
- Update via SD: looks for UTX-1013.bin, FSI-1013.bin, DAN-1013.bin, YPK-1013.bin; 9 check-values appended (per-bit byte index counts + byte sum, minus last 72 bytes); author wrote flashfilepacker [p40, p43]. FSI-1014.bin renamed hung at startup bitmap [p40]. Unit entered hardware-check mode after custom update [p43].
- Init data is RLE-packed in ROM (2584 non-zero bytes, 7055620 zero, expanded 42756 bytes; touch config at offset 41190) [p31].
- Startup: stock main() order sys_clock_init, memory init, UART0, timer, SPI0, fpga_init, turn off brightness, display init, display lib setup, tp_i2c_setup [p32 #781]. Interrupts: TIMER0 (1 ms), USB_OTG, PORTE (power monitor) [p31].
- Battery monitor sample counts per timebase etc. [p36 #893]. Self-test mode: touch four corners + centre, relay capacitance check; bypass flag in flash [p31, p54].
- Saved files: .bmp/.wav 15000 B; settings in first 1000 B as LE shorts; CH1 trace 1500 samples at +1000, CH2 +4000, Y coords +7000/+8500; settings offsets listed on p41 (run mode 0x00, CH1 enable 0x02 ... brightness 0x78 ... charge 0x190/0x192) [p41]. Stock Y scaling constant 0x38E38E39 (/3.6167) [p42].
- Stock cursor shows up to GHz; bug V1=201V with both channels off [p42].
- Stock FFT shows vertical lines only, no frequency/level readouts [p53].
- Stock averages captures; Vrms etc.; low-signal squelch function FUN_80006654 pulls trace to average if pk-pk <20 (35 at lowest V/div) [p40].
- Long timebase stock: 10 samples per 4 ms at 100 ms/div averaged over two pixels; each slower setting lengthens loop [p39 #960]; undersampling >= 100 ms/div (reads 10 bytes, sum/10) [p38 #946]; single trigger capped at 10 ms/div, normal 50 ms/div [p38 #938].
- Up-sampling in stock: x2 at 250 ns, x5 at 100 ns, x10 at 50 ns (inserted samples), 25 ns x10, 10 ns x25 using 10.22 fixed point [p38-p43]. Triangle-ish averaging gives frequency-independent rounding [p43].
- Hardware variants run the same stock firmware except touch/display; 1014D reuses 1013D FPGA/sampling code [p39 #951].

### 4.2 Bootloader / how the open-source firmware is loaded
- FEL path: `sunxi-fel -p spl fnirsi_1013d_startup_with_fel.bin` then `sunxi-fel -p write 0x7FFFFFE0 fnirsi_1013d_scope.bin exe 0x80000000` [p37 #924]. Linker origin 0x7FFFFFE0 (32-byte eGON header before 0x80000000); header 12 bytes filled later by `mksunxi`; eGON.EXE marker [p36, p45].
- FEL bootloader on SD: write `fel-sdboot.sunxi` with `dd ... bs=1024 seek=8`; `sunxi-fel version` -> AWUSBFEX; read flash: `sudo ./sunxi-fel -p spiflash-read 0 2097152 w25q16_new_scope.bin` [p43, p54].
- Final scheme (SD boot): firmware (~268 KB, `fnirsi_1013d.bin`) in raw sectors at SD sector 16 (`dd bs=1024 seek=8`), display/touch config at sector 710 (`seek=355`, 0.5 kB), no filesystem needed for these; SD partition must start >= sector 2048; removal: zero sector 16; stock firmware used if card absent [p52 #1277, #1281; p56; p59]. Flash is untouched; "bricking near impossible" [p45 #1124]. Use block device (sdb) not partition (sdb1) [p57 #1406].
- Config sector: two 32-bit markers, two settings, two markers, 32-bit sum checksum; touch swap bytes appended after checksum (0xFF normal, 0x00 swap/invert) [p52, p56 #1381].
- Two-stage loader: stage 1 (17 KB) loads startup-screen program, which loads main program; a bare variant skips startup screen (untested) [p45, p53 #1303]. BROM header checksum, 64-byte SPI FIFO bug in early loader [p31].
- Backup tool: `fnirsi_1013d_fwb.bin` on SD; writes FNIRSI_1013D_full_flash_backup.bin, FNIRSI_1013D_tp_config.bin, FWB_FSI_1013.bin; detects 2/4 MB by flash ID [p45, p57].
- Recovery: restore via backup + sunxi-fel; a `tp_xy_swap` sector exists [p58].
- Linux 5.x boots on the scope via u-boot SPL (iscle/pcprogrammer); abandoned for bare metal [p35, p37].

### 4.3 Open-source firmware architecture and status
- Bare metal, C, GNU ARM toolchain (arm-none-eabi-gcc), NetBeans 8.2 projects, Linux Mint 20; three projects (bootloader, startup screen, `fnirsi_1013d_scope`) built via makefile; linker flags `-T ... -nostdlib -lgcc`; -O2 works, -O1 broke touch until asm delay [p38, p47, p53].
- Components: own display library, own fonts, FatFs (latest, LFN), own sunxi-mmc based SD driver, USB MSC from sunxi sources (5-20 KB), tp I2C bit-bang, flash R/W, settings, caches/MMU enabled (~2x speedup), scope simulator for x86-64 with stubbed FPGA functions and random errors [p36, p41, p44, p45, p46, p47, p48].
- Emulator by author (Xlib, ARM/Thumb, own trace system) used for reversing from p32-p37; also a QEMU machine (cool2000) [p58].
- Code size: ours 183-211 KB vs stock 1.6 MB [p41, p45].
- Release timeline: Oct 2021 status list (system, touch, flash, SD, FPGA interface, long timebase 50 s-100 ms done, short 50 ms-10 ns partial) [p42]; Nov 2021 tests; Dec 2021 test firmware with ACQ menu; V0.001 (Jan 2022), V0.002 (separate sample rate, SD settings, picture/waveform view, X-Y, vertical zoom, auto set), V0.003 (Feb 21 2022: all measurements, measurement layout CH1 left, CH2 right), V0.004 (touch swap config), V0.005 (auto-set freeze fix, NORMAL/SINGLE blank display fix; Mar 2022) [p52, p55, p56].
- Features of new fw: separate sample-rate and time/div (ACQ menu, 18 sample rates, 200 ms/div to 5 ns/div), tap left/right edges change timebase, 3000 samples, no special IC, soft float (no FPU; plan fixed-point), picture/waveform save to \pictures and \waveforms with 1000 item limit, X-Y mode, vertical zoom in stop/waveform view, auto-set, 50% trigger, measurements, USB MSC, version under battery icon [p48, p52, p55, p56].
- Missing in new fw: FFT (menu entry without function), math channels beyond stock, trigger-level volt scaling, roll mode removed, proper DC handling in auto-set [p52, p57].
- Signal generator front panel/simulator work (CH1 output; for Lichee Nano + FA201 AWG side project) [p48, p49].
- Stock firmware remains restorable; support-supplied June 2022 update (1013D固件.7z) matched GitHub version with display-type binaries differing [p57].
- Calibration persisted to SD in new fw; calibration needed after V0.002 format change [p52 #1299]. Calibration capture tool `fnirsi_1013d_calib_capture.bin` writes calibration_data.bin ~1.8 MB to SD [p51].
- Sample-save file for new fw .wav holds full 3000 samples per channel; roberttidey tool misses pulses at 1 S/div [p57, p58]. No CSV/PC streaming export exists [p56 #1398, p57].
- Known bugs (new fw, 2022): trigger offset at >1 MHz [p52 #1296]; jitter attributed by author to PSU/conversion, others to sync [p57]; auto-set with large DC fails [p52]; Rufus-image users saw channels jump with auto-set + calibration residual [p60 #1491]; CH1 off auto-set bug fixed [p52-p53 #1298-1300]; frequency measure at 200 ns-5 ns fixed [p52 #1284/1286].

## 5. Reverse-engineering methods and tools used (reusable)
- Ghidra on flash dumps; decompiler gotchas (missed gotos, divide-by-3 magic constants, 64-bit casts) [p32-p39].
- Logic analyzers: Saleae Logic 2 (converter for binary export), DSLogic (DSView 1.12), 24 MHz 8ch clones, PulseView/sigrok; sample counts varied at 4 MHz, 12 MHz better [p33-p35].
- Rigol MSO for pin timing [p31, p40].
- SD+FEL workflow, qemu/emulator tracing for per-instruction state [p32-p37, p58].
- FPGA bitstream: prjtang format, Tang Dynasty IDE databases; abandoned bitstream-to-netlist RE (Feb 2022) after tile/bit map but without routing [p54 #1351, #1354]. Original bitstream .bit recoverable from flash .bin by recalculating CRC in header [p54 #1341]. Programmed via Anlogic programmer (STM32F103 "Blue Pill" clone with 74LVC125 level shift) or OpenOCD+USB-Blaster clone (fails at 95%) [p49-p51]. Clone USB-Blaster ~25 MHz; use of SVF via TD IDE works [p51].
- New FPGA design: morris6 (trigger problematic) and author's crude Verilog (Nov 2022) [p55, p59]; license Anlogic_20220130.lic / 20220703 for TD [p51, p53].

## 6. Measurements and known limitations summary
- Real-time 200 MSa/s per channel, 8-bit, 3000-sample buffer = 15 µs record; usable BW ~30-40 MHz; stock fw fakes >43.3 MHz [p46, p48, p59].
- Memory depth tiny: 3000 samples (FPGA has 270 Kbit; 8 MB desired) [p48, p55].
- Analog: single 1.25 V virtual ground, poor grounding, ripple on rails, 6 HW ranges, 50 mV/div software zoom, offset via PWM at ADC input only [p50, p57].
- Trigger: poor below ~200 mV in stock fw; trigger limited to modes 10 ms/div single and 50 ms normal; roll mode below 100 ms/div [p33, p35, p38].
- Interleaving mismatch between ADC pair causes ripple/steps; compensation per V/div and DC offset required [p49-p51].
- Aliasing: no anti-alias filter; undersampling at long timebases (averaging 10 samples) [p38-p39].
- Touch: unit-specific inversions; freezes while touched in stock fw [p47, p56].
- Reliability: assorted dead units (touch, MIC29302, FPGA solder, F1C100S) [p46, p53-p54, p60].

## 7. Ideas / wishes not implemented (as of Dec 2022)
- USB/PC streaming or CSV export; only MSC exists. USB to-do on p42; RogueSS asked for CSV export [p56]. Author considered a serial-like CDC (CH340 descriptors test froze scope) [p44].
- Deeper memory: 8 MB with AL3-10S or other FPGA [p55]; EP4CE6 as CE10 for bigger buffers [p49].
- New FPGA design (more samples, proper trigger with position, raw 8-bit data with MCU offset, interleaving in FPGA) [p48, p49, p55, p59].
- FFT with proper axes, CMSIS DSP [p41, p56]; sinc interpolation, averaging, dot display [p47-p48]; histogram persistence display (rhb proposal) [p39 #967].
- Fixed-point math; DMA for SD/USB; DEBE/DEFE layer offload for UI [p38, p41].
- Hardware: -2.5 V supply for input op-amps; rebuild front end; higher LPF corner; external preamps; BMS cutoff [p49, p55, p60].
- Per-V/div x ADC x sample-rate calibration tables (6 ranges x 4 ADCs = 6 KB, x18 rates = 90 KB) [p52 #1275].
- Dropdown for time/div, user-selectable sample rate, trigger menu rework, filename tap toggles [p48, p52].
- Signal generator using FA201 + AD9226/AN9767 modules (side project) [p53, p56].
- Windows-friendly SD image (Rufus image added to repo) [p60].

## 8. Open questions / contradictions
1. FPGA identity: early units have Altera EP4CE6E22C8N; later units AL3A10LG144C7 with JTAG IDCODE 0x18006C31; morris6 and pcprogrammer disagree at times on clone vs different family (resolved to "different FPGA" on p50-p51). Which board revisions carry which is not fully mapped (AliExpress units from the same period differ) [p47-p51, p55].
2. ADC count: two per channel (p49-p51) vs "4 ADCs" (p52) vs interleaving described as CPU-side vs FPGA-side [p42, p48]. ADC part number never stated in the slice.
3. Touch controller: GT911 vs GT915 naming [p31/p32]; 1024x600 config vs 800x480 panel.
4. DDR size: 32 MB (author, p39) vs 64 MB (DavidAlfa assumption); DDR clock 156 vs 204 vs 312 "MHz" quoted ambiguously [p39-p40].
5. Flash size: W25Q16 2 MB stated for new unit vs "original has 4 MB" [p43]; SPI clock divisor AHB/4 vs /6 vs /12 inconsistent [p46].
6. Reference voltage 1.25 V vs "2.5 V" (Evi) [p60]; TL431 vs TL432 pairs [p55].
7. Special IC: first thought to hold settings (p33), later a translation chip (p35), later "unneeded and harmful" (p50, p56). Full command set bypassing it only partly documented (0x11 range unknown).
8. Offset: command IDs 0x32/0x35 (p35) vs PWM nets (p50); the relationship of those commands to the 25 kHz PWM is inferred, not confirmed.
9. Samples per capture: 750/1500/2500/3000 quoted by different posts [p34, p40, p48, p52]; original read counts differ by timebase.
10. Trigger: where trigger point sits (mid-buffer assumption), meaning of cmds 0x05/0x0A/0x0F/0x1F unconfirmed [p49-p50]; trigger offset at >1 MHz unresolved [p52].
11. Stock behavior above 43 MHz fabricated (p46) vs bianchifan's "good to 25 MHz"; real bandwidth 30 vs 40 MHz [p49, p59].
12. Tool/instruction typos flagged by posters: Mac dd device names disk7/disk8 [p53]; `of=/dev/sdb1` should be block device [p57]; Vpp of mains 780 V (should be ~680 V) [p53].
13. Page 38 and page 42 fetch results were truncated mid-post (final ~10 K and ~20 K characters); page 43 final 1.7 K chars unread. Data from those tails is partial.
