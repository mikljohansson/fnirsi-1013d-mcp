# EEVblog thread "FNIRSI-1013D 100MHz tablet oscilloscope" - digest of pages 1-30

Thread: https://www.eevblog.com/forum/testgear/fnirsi-1013d-100mhz-tablet-oscilloscope/ (topic 238226). Page N = offset (N-1)*25. Covers posts #0-#749, 2020-04-18 to 2021-04-23.
Citation form: [pN, poster]. Page numbers are by URL offset. Facts were extracted by a summarizing fetch tool, so exact wording and some numbers should be spot-checked against the source before being relied on. Items marked (INFERENCE) are mine.

## 1. Product, variants, branding

- Same hardware sold as FNIRSI-1013D, DANIU ADS1013D (Banggood), Yeapook (Amazon US, "Ccfoud-US" seller; Shenzhen Yipu), Elikliv (Amazon), "nano1013d". [p2 gfmucci; p5 skoehler; p9 cliffyk, p10 cliffyk]
- Firmware binaries in dump are rebranded: UTX-1013.bin, FSI-1013.bin, YPK-1013.bin, DAN-1013.bin. [p9, rhb #213]. Later: FSI-1013en.bin (English) vs Chinese build. [p24, ser8989]
- Prices: ~$140 AliExpress, $118-148 Banggood, $150-165 Amazon US, 699 RMB (~$100) Taobao, ~100 EUR AliExpress in 2021. [p1; p2; p9; p14 UniSoft; p30 pcprogrammer]
- Seller listing claims: 100 MHz analog BW x2 ch, 1 GSa/s, 8-bit ADC, 50 mV/div-500 V/div, timebase 50 s/div-10 ns/div, 240 Kbit storage depth, 1 Mohm, 7" 800x480 capacitive touch, 6000 mAh battery, 184x124x50 mm, 650 g, rise time <3 ns, 1X probe 5 MHz, 10X 100 MHz, 12 auto measurements, FFT, XY (Lissajous), 1 GB storage = 1000 screenshots + 1000 waveforms, 5V 2A charger. [p1, Valentin]
- FNIRSI's reply to a customer: "100 MHz" is the "maximum theoretical value". Image: http://www.paladinmicro.com/TestEquipment/Yeapook/FNIRSIResponse-00.jpg [p12 cliffyk]
- Predecessor FNIRSI-5012H: same lineage, software-triggered, no FPGA, ataradov wrote open firmware for it. [p1 Kosmic; p3 maginnovision]
- Hardware revisions:
  - Blue PCB (early) vs green PCB (later, better length matching) [p5 skoehler, wolfy007].
  - "Newer"/"II" version (Dec 2020): recessed BNCs and power switch, tempered glass front, W25Q32 (32 Mbit) flash instead of W25Q16, no RESET solder pad, metal shield cans over analog front end, different front end layout, 8751-marked opamp instead of OAAI (OPA356). [p18 hughtmccullough, pickle9000, MLutz; p19 LeisureSuitLarry, eljot, pjw1234; p23 pcprogrammer]
  - Old units: pin 70 of F1C100S broken out as pad; new units: must touch the pull-up resistor. [p8 iscle; p19 LeisureSuitLarry; p26 pcprogrammer]
  - Newer version connects flash CS to the slot (and possibly the SoC); older tied CS permanently to Vcc. [p19, eljot]
  - Official product page updated Dec 2020: http://www.fnirsi.cn/productinfo/556152.html [p19 Kean]
- Support: http://www.fnirsi.cn/support (manual). English manual: http://www.paladinmicro.com/TestEquipment/FNIRSI-1013D English manual.pdf (47.5 MB, manual has a "Solemn reminder" section) [p5 tunk; p7 cliffyk]. FNIRSI declined replacement parts and answered slowly. [p21 tunk; p22 pcprogrammer]
- Reviews/teardown links: mysku.ru/blog/aliexpress/79977.html [p5 tunk], mysku.ru/blog/aliexpress/80036.html (input channel schematic by "St_dsk") [p11 UniSoft], https://chinese-electronics-products-tested.blogspot.com/p/ads1013d-tablet-oscilloscope-tested.html (rise time 21.2 ns => ~16.5 MHz) [p19 Trader, cliffyk]. Dave Jones (EEVblog) teardown photo: https://flic.kr/p/2jhUqgU [p6].

## 2. Hardware identification

| Block | Part | Source |
|---|---|---|
| SoC | Allwinner F1C100s (ARM926EJ-S, ARMv5TEJ, DDR on-package). Ghidra setting: ARM v5t little-endian (BLX used); v4t also worked | [p1 1847123212; p12 UniSoft #279; p24 pcprogrammer #586] |
| ADC | AD9288 (dual 8-bit, 100 MSa/s per core), one chip per channel (2 total). Markings sanded off; identified from pinout, firmware strings, ENC A/B pins | [p6 UniSoft, rf-loop, EEVblog #137/#142; p9 rhb #213/#221] |
| Front-end opamp | OPA356 (marked "OAAI" on old board; "8751 2023" on new board, possibly Runic RS8751/RS8754) | [p15 hughtmccullough #357; p23 pcprogrammer #563, UniSoft #565] |
| AC/DC relays | KAQY214S solid-state relays (firmware checks for failure) | [p30 pcprogrammer #727] |
| FPGA (old units) | Altera, Cyclone IV EP4CE6 (270 kbit RAM); bitstream parsed as Altera RBF, AS x1, compressed | [p10 rhb #240; p18 SmokeyTheElectrician #429; p11 tv84 #269; p24 pcprogrammer #584] |
| FPGA (newer units) | pcprogrammer's repo name says Anlogic AL3-10; later posts still say Cyclone IV. See Open questions | [p19 sig; p27 #656] |
| FPGA config flash | Old: ST 25P16VP (16 Mbit SPI NOR), dedicated FPGA pins, only connects to FPGA. New: 25Q80 / W25Q80 (8 Mbit). Config takes ~1 s, flash clocked ~2.4 MHz | [p11 UniSoft #262/#265; p23 dmitrkov #550, pcprogrammer #551; p30 #741] |
| FPGA clock | 50 MHz oscillator next to FPGA | [p23 pcprogrammer #551] |
| Main flash | Winbond W25Q16 (2 MB) on old; W25Q32 (4 MB) on new. Holds firmware (F1C100S boots from it). Whole image is one SPI dump, no second flash for the CPU | [p9 UniSoft #212; p19 eljot; p10 UniSoft #245] |
| Settings EEPROM | 8-pin I2C EEPROM next to FPGA below oscillator, markings erased; pin 7 (WP) floating so not 24LC04, maybe N24C64. Only 2 FPGA connections. (Later reanalysis: FPGA talks to an "unknown I2C chip" via 0x6X commands) | [p11 UniSoft/iscle; p22 pcprogrammer #548; p29 #723] |
| Display | 7" 800x480, panel marked HT070QB-27W2012HZ, ~565 RGB with sync+clock, 50-pin FFC; active area 164x99 mm. Connects to F1C100S TCON; works in Linux with "innolux,at070tn92" timing (LG LB070WV8 compat string was the stock lichee-nano DTS) | [p23 #567; p9; p26 #625/#626] |
| Touch | Goodix GT911 (also seen: GT915 as replacement, GT9157 on replacement glass). 6-pin FFC: 1 RST, 2 Vcc, 3 GND, 4 INT, 5 SDA, 6 SCL. Panel uses 10 RX / 16 TX of controller's 14/26. I2C address 0x14 (RST/INT strapping) | [p11 UniSoft; p20 pcprogrammer #494; p22 eljot #527, pcprogrammer #538; p27 #674] |
| Touch I2C | Bit-banged in software on port A (PA3=SCL, PA2=SDA, PA0=RST, PA1=INT), ~40 kHz. Firmware polls register 0x814E rather than using INT. 10k pull-ups (marked 01C) ~7.5k measured plus a strange capacitor across SDA-SCL (100 pF fixed one unit with erratic touch) | [p17 Dbuezas; p22 pcprogrammer #536; p24 #575; p28 #679] |
| SD card | 1 GB (SanDisk-branded) microSD supplied; holds only screenshots (BMP 800,000 B) and waveforms (15,000 B). 32 GB FAT32 works; works without card but hangs on Save; "SD ERROR" red message on bad card | [p3 boggis; p4 AlcidePiR2; p10 UniSoft #249; p18 MLutz] |
| USB | Handled by SoC (host/peripheral). Presents as mass storage to PC for screenshots. Connecting a source while on PC USB can offset waveform (ground loop / isolation) | [p8 iscle; p13 Martin72; p8 cliffyk] |
| Battery | 6000 mAh Li-ion, user upgradable (space in case). 6h45m measured runtime; ~3.5 h recharge, 1.8 A tapering to 0.3 A. Micro-USB power (2 A optimistic). Charging from some USB stations can cause faults (ground loop) | [p7; p8; p15 cliffyk #368] |
| Power | AMS1117 3.3V LDO on board; flash VCC fed through a diode. Power rails also 2.5V and VRF1 (ADC reference) | [p27 #663; p27 #660] |
| Inputs | BNC, 1 Mohm, ~30 pF (CH1 1.015 Mohm/30.0-30.1 pF; CH2 1.012 Mohm/30.5 pF). BNC shields tied together (0 ohm) i.e. not isolated channel-to-channel | [p3 boggis; p5 AlcidePiR2, wolfy007] |
| Gain ranges | No variable-gain amp. Fixed attenuator + OPA356 stage; 50 mV/div is software scaling (INFERENCE from hughtmccullough: ~7 bits effective). Real min sensitivity 50 mV/div (500 mV/div with 10X probe) | [p15 hughtmccullough #357; p7 cliffyk, tautech; p13 rhb #302/#312] |
| ADC-FPGA routing | Meandered length-matched traces; PCB lacks routing for bits 9-10 (no 10-bit upgrade without respin). AD9218 suggested as 10-bit drop-in by rlohmann | [p5 rlohmann #110] |
| Front-end sim | Simulation from mysku (by St_dsk): lowering R12 to 10-22 ohm flattens response to 100 MHz; raising C2 flattens LF. Image https://pic.mysku-st.ru/uploads/pictures/10/83/75/2020/06/25/bddbbe.jpg | [p14 GeorgeOfTheJungle #344; p15 hughtmccullough] |
| Test access | Pin 70 of F1C100S = RESET (short to GND to free SPI bus); unpopulated 2.54 mm headers expose flash pins; 6-pin header near flash chips and 4-pin FPGA programming header (unpopulated). SOIC-8 clip works. No UART header. UART0 pins go to FPGA; UART1 shares touch connector pins PA2/PA3 | [p8 iscle #179/#181; p12; p26 #613] |

Schematic reconstructed by pcprogrammer (March 2021): Schematic_Scope_2021-03-23/26 PNG/PDF; capacitor values mostly guessed (1 nF placeholders), ferrites guessed, analog correctness uncertain. In repo https://github.com/pecostm32/FNIRSI-1013D-Hack (Schematics and pictures folders). LCD pinout image lcd_pinout.png from UniSoft. [p23 #561-#570; p27 #660]
FPGA-to-LCD: LCD connects via 11 FPGA-side port pins PE0-PE10 (that is actually the CPU-FPGA bus; see section 3) [p23 #567 says "connects to the FPGA via 11 port pins"]. Backlight enable is controlled by FPGA pin driving a backlight converter enable pulled low. [p17 iscle #417; p24 #597]

## 3. CPU-FPGA interface (reverse engineered from firmware)

- Port E (base 0x01C20890 config register, data 0x01C208A0) pins form an 8-bit parallel bus to the FPGA:
  - PE0-PE7 data bus
  - PE8 clock (strobe)
  - PE9 R/W (0 = read, 1 = write)
  - PE10 CMD/DATA (0 = data, 1 = command)
  [p29 pcprogrammer #701; p28 iscle #697 "PE9 looks like W/!R, PE10 like CMD/!DATA"]
- Pin config registers use 4 bits per pin. Output = 0001 pattern per nibble (0x11111111 for 8 pins), input = 0x00000000; writing all 1s disables. Pn_BASE(n) = 0x01C20800 + n*0x24. [p28 #698; p29 #700, #703-#705 iscle, tv84]
- Write data with AND 0xFF so other pins are not disturbed. [p28 #698]
- Low-level functions (original firmware, second executable, DRAM addresses): FUN_800169F8 write command, FUN_800168FC write data, FUN_80016850 read data (sets PE0-7 inputs, pulses PE8 low then high, reads low byte), FUN_800167A0 set bus to input, FPGA comms init FUN_800168A0. [p28 #682; p29 #713; p25 tv84 #604]
- 42 distinct commands found written to the FPGA. List posted at https://github.com/pecostm32/FNIRSI-1013D-Hack/tree/main/Software%20reverse%20engineering/C%20analysis/FPGA%20analysis [p29 #713, #718]
- Known commands:

| Cmd | Meaning | Source |
|---|---|---|
| 0x06 | read FPGA version; returns 0x1432; use as FPGA-ready poll (~350 ms after power-up) | [p29 tv84 #724; p30 #743] |
| 0x38 | LCD backlight: followed by 2 data bytes, 0xEA then level (default 0x60 seen); used at init and brightness menu | [p28 iscle #697; p29 #710, #717, #720] |
| 0x41 | check command; reads 2 bytes compared with 0x8150 (labelled "FPGA_Encrypt" checksum check) | [p29 tv84; p30 pcprogrammer #725] |
| 0x21 | set up reading of 1500 or 750 bytes (sample/display buffer) | [p29 #701] |
| 0x64 | prepare read of I2C-EEPROM param (send twice) | [p30 #738] |
| 0x65 | prepare write (send twice) | [p30 #738] |
| 0x66 | start | [p30 #738] |
| 0x67 | read status, bit0 must become 1 | [p30 #738] |
| 0x68 | memory read selector (0x09 seen) | [p30 #738] |
| 0x69 | parameter ID (6 bit) + byte count (2 bit) | [p30 #738] |
| 0x6A | checksum = sum of other six bytes | [p30 #738] |
| 0x6B-0x6E | value bytes 3 (MSB) .. 0 (LSB) | [p30 #738] |

  Param read retries 50x, whole sequence 6x (FUN_800248f8). Settings transfers are 8-byte bursts (1 slave addr + 7 data). FUN_80024b14 inverts received bits based on 0x68 byte; working buffer in DRAM 0x8035EC04. [p30 #738-#739]
- Boot-time hardware check (Check_HW, tv84 decompile): 1) FPGA ID read cmd 6 == 0x1432; 2) FPGA_Encrypt checksum == 0x8150; 3) AD9288 check reporting AD9288_1 / AD9288_2; 4) analog check (relay failure detection KAQY214S); 5) touch check. Success prints "Hard Checked Successful !". Missing/failed -> error text e.g. "FPGA Failed". [p29 #724; p10 iscle #243; p30 #727]
- Expected (not yet verified): relay control, sample rate, trigger params, ADC offset (ADC1_OFFSET/ADC2_OFFSET come from FPGA), vertical gain commands are among the remaining 0x6X and other commands. [p29 pcprogrammer #713, #718]
- Waveform buffer transfer: wav file layout (below) indicates 1500-sample blocks per channel; display data 750 points. Capture memory limited: Cyclone IV EP4CE6 270 kbit / 9 bits = 30,000 -> 15,000 samples per channel (SmokeyTheElectrician hypothesis, 15,000-byte wave file). [p18 #429] (INFERENCE: FPGA hands CPU decimated/limited buffer, not full sample memory.)
- Bus timing: CPU switches to 600 MHz in original firmware (default PLL_CPU 0x80011000 gives 204 MHz; manual says 408 MHz). Clock pulses for FPGA made with function calls; delays only between some FPGA actions (sub_8000BC34 = delay). [p30 #734, #737]
- UART0 base 0x01C25000 (data) / 0x01C2507C (status) in original firmware sends debug strings only (e.g. "D_info->sectorSize"); does not use port E. [p28 #695]

## 4. Sampling, ADC and analog performance

- True sample rate: 200 MS/s per channel. Both cores of each AD9288 are clocked out of phase (biphase, ENC A/B at 100 MHz) and fed the same signal. No combining of channels (4 cores cannot merge). [p6 UniSoft, rf-loop, EEVblog #139/#142, skoehler #141]
- Marketing 1 GS/s is ~5x too high. EEVblog suggests 20 MHz rating; skoehler 1:5 rule gives 40 MHz; real single-shot usable BW 20-40 MHz. [p6]
- gf asks whether ADC phases stay anti-phase in 2-ch mode (Hantek 2000 shows half-clock skew between ch). Unanswered. [p15 #355]
- Measured -3 dB bandwidth:

| Setup | Result | Source |
|---|---|---|
| 50 ohm patch cable, X1 | 20 MHz | [p8 cliffyk #175] |
| Alligator clips, X1 | 33-34 MHz (+0.58 dB at 10 MHz, -1.26 dB at 30 MHz, -3.24 at 34 MHz) | [p9 #200] |
| 16 in 50 ohm cable, alligator, X1 | 34 MHz | [p14 #335] |
| Supplied probe X1 | 18.6 MHz; Martin72 17.8 MHz | [p14 #338; p13 #323] |
| Active 1 Mohm head X1, boggis | ~50 MHz but amplitude varies >30 MHz; -2.3 dB at 50 MHz; "20 MHz scope, -3 dB at 30-40 MHz" | [p9 #204; p15 #359] |
| Martin72 1 Vrms sine | -3 dB at ~37 MHz | [p13 #310] |
| pcprogrammer (AFG3102) | -3 dB at ~18 MHz (7 div at 1 kHz -> ~5 div) | [p19 #470] |
| wolfy007 three units, HP 500 MHz probes | usable sine to ~85 MHz with heavy attenuation/AM artifacts; est. 40 MHz | [p5 #118] |
| X10 with compensation tweaking | peaky response, 85 MHz sine visible; response changes with V/div | [p9 #202; p16 cliffyk #398] |

- Rise time: measured ~12 ns (Leo Bodnar), claimed <3 ns; 17 ns estimate from capture; 21.2 ns in review. [p4 AlcidePiR2; p6 #147; p9 rf-loop #211; p19 #459]
- Vertical accuracy: boggis saw 212 mVpp for 200 mVpp (6% high); cliffyk notes ±1.5% FS is typical of 8-bit scopes. [p2; p9 #205]
- Trigger: software-like jitter above ~10 MHz; ±0.1 div jitter at 50 kHz; 10 MHz stable, unstable near 20 MHz; 100 MHz trace only appears when timebase ramped 11->10 ns/div. [p2 boggis]
- Interleaving/other artifacts: CH2 glitch at 25 ns and 10 ns/div (7-14 MHz signals), cleared temporarily with Autoset cycling; CH1 noise appears on CH2; pjw1234 concludes not fixable by replacing one part; replaced OPA356 and AD9288 with no change; CH1 reaches 82 MHz but CH2 only 70 MHz. cliffyk guesses missing buffer data masked by sin(x)/x interpolation. [p19 #464-#467; p20 #492, #499; p21 #501-#502]
- Intermittent 25 kHz ~20 Vpp artifact on both channels (alangave); suspects shared rails 3.3V/2.5V/VRF1 or backlight PWM; unresolved. [p27 #659-#672; p30 #728-#731]
- Real V/div ladder from firmware (as listed, includes typos): 5 V, 2.5 V, 500 mV, 200, 100, 50 mV; 25 V... Timebase 50 s/div-10 ns/div: 50,20,10,5,2,1 s, 500..., 25 ns, 10 ns. Wave file vertical scale code 0-6 = 5 V..50 mV/div; time scale code 0-29. [p9 rhb #213; p18 btidey #431]
- Timebase: no triggered sweep slower than 10 ms/div; scroll (roll) mode from 100 ms/div (untriggered); at 20 ms/div trigger markers vanish; Normal trigger mode UI sluggish without trigger. [p8 cliffyk #207; p10 #231; p13 #309]
- Trigger options: edge rise/fall, single/normal/auto; no runt/glitch/serial triggers; frequency measurement works only in auto trigger; no zoom after stop in some units. [p14 Martin72 #346]
- FFT: scale tied to timebase, vertical scale tied to V/div, cropped at top, no labels, no cursors, fixed origin 0 Hz; alternation of tall/short peaks due to bin alignment. [p3 AlcidePiR2 #69/#74; p8 cliffyk]
- Display of waveform: only sin(x)/x "vector" mode, no dots mode. XY mode works but limited to one screen; screen capture BMP 800,000 B ~2 s; wave save 15,000 B ~2.5 s. [p3; p9 #215]
- Rejects: no setup save/recall; powers up in last state; no units option for current probes. [p2; p14]
- 10X/100X switching is only on-screen scaling; attenuation is in probe. [p21 Fungus #524; p22 wolfy007 #525]
- Safety: not isolated between BNC grounds; not for mains. Battery operation gives floating measurement. [p1 nctnico; p3 boggis]
- Probe notes: bundled P6100-style probes are poor (~25 MHz) compared with generic P6100 (~50 MHz); 1X probe BW stated 5 MHz in manual. [p16 Kosmic #395; p14 cliffyk #335]
- Rhb: lack of VGA limits range; no anti-alias before VGA; recommends input LP corner at 40-50% of Nyquist (30-35 MHz for 100 MHz Nyquist). [p13 #302; p14 #345]

## 5. Original firmware and flash layout

- SPI flash dump (W25Q16, 2 MB; posted by UniSoft July 2020, attachment W25Q16.zip; later copies: LeisureSuitLarry p19 incl. Chinese build; eljot W25Q32.zip https://www.eevblog.com/forum/testgear/fnirsi-1013d-100mhz-tablet-oscilloscope/?action=dlattach;attach=1154712 [p20 #478]; dmitrkov OneDrive https://1drv.ms/u/s!Av-Riptwsak2iskvoKrSKJR8rpKr_g?e=m4eMUw [p21 #504]). Latest dumps in https://github.com/pecostm32/FNIRSI-1013D-Hack/tree/main/Binaries [p27 #656]
- Dump programmer notes: EZP2019+ gave inconsistent reads; SP16-B (sofi-tech) and CH341A reliable. CH341A drives 5 V I/O; 4.1 V measured on flash VCC (diode); apply 3.3 V mod (chucknemeth.com/usb-devices/ch341a/3v-ch341a-mod). AsProgrammer 2.1.0.13 recommended; ch341prog (setarcos). [p9; p27 #655, #669]
- Layout (tv84, UniSoft; each block has 32-byte eGON header, offset 0x10 size incl. header aligned to 0x200, offset 0x0C SUM32 checksum computed with checksum field preset 0x5F0A6C39; mksunxi.exe does it):

| Flash offset | Size | Content | Load addr |
|---|---|---|---|
| 0x00000000 | 0x3400 | SPL (secondary bootloader, eGON.BT0) | 0x00000000 |
| 0x00006000 | 0x7400 | 1st executable (SD card/OS part, uncertain) | 0x80000000 |
| 0x00013000 | 0xE600 | boot bitmap | - |
| 0x00027000 | 0x193200 | 2nd executable = main app | 0x80000000 |

  [p10 tv84 #241; p11 #270; p12 UniSoft #281]
- SPL behaviour: shows bitmap; reads header of 2nd executable at 0x27000, if valid loads rest to DRAM 0x80000000; on failure tests 1st executable at 0x6000; if both fail endless loop. SPL contains Allwinner sys-dram.c-style DRAM init (dram_get_dram_size at 0x1150). [p30 #749, #744]
- Strings: "F1C100S XiaoTaoQi Disk 1.0" (USB descriptor, mass storage), /pic_system.sys, /piclist.sys, /wave_system.sys, /wavelist.sys, eGON.BMP/EXE, "switch into high speed mode !!!", "Please insert test clip and press OK to continue !", "Encrypt", AD9288/_1/_2/_1_2. [p9 rhb, GeorgeOfTheJungle; p10]
- JuTouch library reference (rhb guess) is based on pic_system.sys; unconfirmed. [p10, p11]
- Large block at ~0x184C80 argued as obfuscated vs ascending table (Unicode/memory map): unresolved in these pages. [p10-p11 UniSoft, tv84]
- No in-field update mechanism; firmware must be reflashed over SPI. Boot via SD does not exist; FEL mode reachable via sunxi-tools fel-sdboot written to an SD card at seek 8, USB ID 1f3a:efe8. Chinese vs English firmware: English writes GT911 config at startup, Chinese does not. [p4/p6 UniSoft #144; p24 pcprogrammer #580; p24 ser8989 #590]
- Original firmware writes touch config from RAM buffer 0x8019CF82 (186 bytes 0xBA, register 0x8047, checksum appended) via FUN_8001794c; NOPing that call (or FUN_ in ser8989 version) stops config overwrite; hacked files https://github.com/pecostm32/FNIRSI-1013D-Hack/tree/main/Binaries/Hacked%20files [p27 #658, #665-#666; p28 #679]
- Settings stored in I2C EEPROM via FPGA commands (section 3), not in SoC flash. [p29 #723; p30 #738]
- Setting `W25Q32 reset vector jumps to 0x80000000` on newer flash version. [p30 #743]

### Wave file format (15,000 bytes) [p18 btidey #431-#440]
- Bytes 0-999 header; 1000-3999 CH1 data1; 4000-6999 CH2 data1; 7000-8499 CH1 data2; 8500-9999 CH2 data2; 10000-14999 nulls.
- CH1 vertical scale byte 4, CH2 byte 14; time scale byte 22 (repeated 52); CH1 measurements bytes 208-255 (12 x 4-byte), CH2 256-303.
- DataBlock1: 1500 little-endian 16-bit values per channel (buffer, 0-399 range, 200 = zero, vertically mirrored); DataBlock2: 750 display Y offsets; grid 50x50 px; 750 px active width in 800x400 BMP.
- Sign of voltage measurements not saved. Tools: https://github.com/roberttidey/FNISR1013DScope (FNISR1013D-JSON.py), https://github.com/wavoigt/FNIRSI-1013D-WAV-Viewer-in-Excel-VBA [p22 wavoigt].

## 6. Linux port experiments (not the open-source scope firmware)

- iscle and pcprogrammer ported Lichee Nano Buildroot + U-Boot 2018.01 to the scope; scope boots Linux (root/root), display works with innolux,at070tn92 DTS, LVGL demo runs; no backlight until FPGA is initialised. [p24-p26]
- UART: UART0 goes to FPGA; kernel console moved to UART1 (PA2/PA3, shared with touch connector); U-Boot patched (board.c, gpio.h, sunxi-common.h, lichee_nano_defconfig CONFIG_CONS_INDEX=2). UART0 RX left floating stops Lichee Nano boot. [p25 #613-#620]
- Reset trick needed on new boards (tiny SMD resistor); backlight workaround: boot without SD card, insert card after backlight lit, pulse reset. Images: FNIRSI-1013D-Hack/Linux/images/ (lichee_nano_both_uart1, lichee_nano_uart1_innolux,at070tn92). [p25 #616; p26 #630]
- USB console experiments (g_serial, USB_TTY) unfinished. [p27 #651, #670]
- FEL boot experiments: GPIO works in FEL; DRAM not enabled so code at 0x80000000 fails; SRAM at 0x0 runs backlight code; FPGA needs ~1 s config before 0x38 accepted; U-Boot backlight code added to mach-sunxi/board.c; waiting loop on cmd 0x06 == 0x1432 makes Linux show on screen (works on old 16 Mbit flash). [p30 #732-#743]
- Repos: https://github.com/pecostm32/FNIRSI-1013D-Hack (Linux, Binaries, Schematics, Software reverse engineering/Analysis and C analysis), https://github.com/pecostm32/FNIRSI_1013D_Firmware (signature link from p19 onward - the later open-source firmware repo; in these pages only referenced as a link), https://github.com/pecostm32/Anlogic_AL3-10_Analyzing, https://github.com/pecostm32/Hantek_DSO2000, https://github.com/pecostm32/STM32F103_GT911_Conf_Writer, https://github.com/pecostm32/STM32F103_GT911_Conf_Reader.
- Other resources: http://nano.lichee.pro/ ; Allwinner F1C100s datasheet https://linux-sunxi.org/images/b/ba/F1C100s_Datasheet_V1.0.pdf ; F1C200s manual https://www.thirtythreeforty.net/posts/2020/02/trying-the-allwinner-f1c200s/Allwinner_F1C200s_User_Manual_V1.1.pdf ; https://linux-sunxi.org/F1C100s ; http://dl.linux-sunxi.org/ ; FEL: https://linux-sunxi.org/FEL#Entering_FEL_mode ; fel-sdboot https://github.com/linux-sunxi/sunxi-tools/raw/master/bin/fel-sdboot.sunxi ; Lichee Pi: https://github.com/Lichee-Pi ; hackaday.io/project/171402 ; https://steward-fu.github.io/website/mcu/lichee-nano/flash_image.htm ; RT-Thread F1C100S issues https://github.com/RT-Thread/rt-thread/issues/3648 ; GT9xx notes http://andr7e.blogspot.com/2015/05/c-goodix-gt9xx.html .

## 7. Touch panel repair knowledge

- Failure modes: unresponsive jumpy touch (fixed with 100 pF cap on SCL line in one unit) [p17]; dead horizontal/vertical band from a shorted TX line in the glass [p20-p22 eljot, pcprogrammer]; mirrored coordinates after replacing with GT915/GT9157 panels [p23-p27].
- Replacement glass: AliExpress 33042057555 and 33003864443 (GT9157 panels, 10 RX/16 TX like original). Replacement may be 6.2" or 7"; coordinates may be mirrored. Front glass and touch panel are glued; hot air 150-400 C with thin blade; easily broken. [p21-p23]
- Programming the SPI flash in-circuit can upset the touch (eljot hypothesis; unproven). GT911 config is non-volatile once written; English firmware rewrites it at boot, which breaks replacement panels that need reversed Sensor/Driver channel order (registers 0x80B7-0x80C4 and 0x80D5-0x80EE; X2Y/inversion in 0x804D). Fix: write correct config with STM32/Arduino tool, then flash firmware with config call NOPed. Do not power touch from 5 V. Use 3.3 V and 2.2k pull-ups when writing. [p24 ser8989; p26-p28 pcprogrammer, dmitrkov]

## 8. Features, UI, usability (original firmware)

- Capacitive single-touch only (no pinch). Horizontal scale: tap left/right of display; vertical via CTRL menu V+/V-. Auto-set quick but inconsistent. Autotrigger-level can be disabled in System menu. FFT toggled in System menu. [p2 boggis; p7 snoopy #174]
- Saved waveforms (not pictures) can be re-viewed, rescaled, measured, FFT'd; cannot return to run. 12 measurements. [p13 cliffyk #309; p18 btidey #440]
- No signal generator. [p24 pcprogrammer #579]
- Boots in <4 s. [p3 AlcidePiR2]

## 9. Ideas and wishes mentioned (not implemented in these pages)

- Open-source firmware / community firmware, common open base across vendors (rhb, iscle, jemangedeslolos). [p9, p12]
- Linux + Qt/LVGL UI (iscle, GeorgeOfTheJungle suggested RT-Thread/xboot, LVGL). [p8]
- Reprogram FPGA with own bitstream, or use USB-OTG console/CDC for PC link (pcprogrammer). [p26 #632, #638]
- Triggered scroll mode, interpolating the trigger point/retiming for lower jitter (rhb: "easily fixable once firmware accessible"). [p13 #313]
- Unlock features visible in firmware tables: serial bus decoding, eye diagram, zone triggering (tv84 claimed). [p14 tv84 #356]
- Dots display mode; setup save/restore; units for current probes; better FFT. [p9, p14]
- AD9218 10-bit ADC (PCB not routed for 9/10 bits). [p5]
- Better front end: reduce R12, add trimmer caps; internal cap tweak for 1X. [p9 Fungus #210; p14]
- 4-channel version; larger battery; silicone case; 50 EUR 500 MSa/s model. [p1; p7]
- STM32 I2C proxy for touch coordinate fix; bit-banged touch driver; hardware fix for inverted coordinates. [p25-p26]
- Measure FPGA command set live (bare-metal via FEL): sample rate, trigger, relays, offsets. [p29 #715; p30]

## 10. Open questions / contradictions

1. FPGA identity: Cyclone IV EP4CE6 (old units, bitstream parsed as Altera AS RBF, 25P16VP) vs "Anlogic AL3-10" (pcprogrammer repo name, newer units, W25Q80 uncompressed bitstream). Pages 1-30 do not state which board revision has which; the "new FPGA bitstream appears uncompressed" remark [p27 #656] suggests a different FPGA on new boards. (INFERENCE.) Verify in later pages.
2. Sample-rate arithmetic: UniSoft says ENC pins run 100 MHz => 200 MS/s total; early confusion about per-core vs per-chip rate. EEVblog/rf-loop consensus: 200 MS/s per channel. Whether two channels' clocks are phase-aligned is unanswered.
3. Touch controller: GT911 vs GT915 vs GT9157 appear for the original/replacement panels; ser8989 calls original "GT915" in one post and GT911 in others.
4. 4-byte vs 24-byte measurement fields in wave file (btidey #431 vs #438).
5. Flash revisions: W25Q16 (old) vs W25Q32 (new), reset vector behaviour differs; eljot's W25Q32 differs in more than size from W25Q16 dump (bitmap only per pcprogrammer #656).
6. Block at 0x184C80: obfuscated code vs lookup table vs FPGA bitstream (unresolved in these pages; later finding: bitstream is in separate FPGA flash).
7. "Encrypt" check at cmd 0x41: compared with 0x8150 (also GT911 coordinate register address); real purpose unknown; scope boots fine without touch.
8. Sample memory: 240 Kbit claimed; 15,000 samples per channel in wave files; FPGA RAM 270 kbit; how much the FPGA actually stores per trigger is unknown.
9. 1X input bandwidth: manual says 5 MHz; measured 18-34 MHz depending on cabling. Probe capacitance effect.
10. CH2-specific glitches at 25/10 ns/div and CH1/CH2 bandwidth asymmetry (82 vs 70 MHz): cause unknown (relay, layout, firmware).
11. Fungus's "units tested to 30 MHz" vs wolfy007 "usable to 85 MHz"; the latter relied on aliasing/peaking.
12. Where firmware calls FPGA init and touch init (no XREFs in Ghidra; fixed by forcing disassembly) and how settings parameters map to EEPROM IDs remains incomplete.
13. Page-labelling: the fetch summarizer occasionally mis-titled page numbers; posts numbers (#) are reliable, page = floor(post/25)+1.
