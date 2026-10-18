# EEVblog thread "FNIRSI-1013D 100MHz tablet oscilloscope" - pages 61-90

Posts #1500-#2249, Dec 2022 - Jun 2024. Page N = offset (N-1)*25. Source: WebFetch digests (summarised by a small model; exact register values and quotes should be re-verified against the thread before relying on them). Citation format [pNN, poster, #post]. Base URL: https://www.eevblog.com/forum/testgear/fnirsi-1013d-100mhz-tablet-oscilloscope/

## 1. Hardware: SoC, FPGA, ADC, analog, power, board

### SoC / memory / boot
- Allwinner F1C100s, ARM926EJ-S (not Cortex-M3). Compiler flags: -mcpu=arm926ej-s -mfloat-abi=soft -O3 -DNO_STDLIB=1 [p81, pcprogrammer, #2006].
- One post links "Allwinner_F1C200s_User_Manual_V1.2.pdf" (GPIO chapter 3.7) [p82, pcprogrammer, #2025]: https://github.com/pecostm32/FNIRSI-1013D-1014D-Hack/blob/main/Manuals/Allwinner_F1C200s_User_Manual_V1.2.pdf . F1C100s named everywhere else.
- 32 MB DRAM shared by program and data; Atlan estimates >=24 MB free [p68, pcprogrammer, #1692]. ~40 KB SRAM at 0x00010000-0x00019FFF, not set up in the linker script [p68, #1692].
- Display config loaded to 0x81BFFC00 (DISPLAY_CONFIG_ADDRESS in display_control.h; pcprogrammer calls the define a bodge). Startup screen at 0x81C00000, rest of program at 0x81C001E0 [p70, Atlan #1747 / pcprogrammer #1748]. Final scope program loaded from sector 92 to 0x80000000 [p71, pcprogrammer, #1756].
- F1C100s USB peripheral poorly documented [p67, pcprogrammer]. USB docs in repo Manuals folder (SCSI_commands_reference.pdf, musbmhdrc_pspgUSB.pdf, usbmassbulk_10.pdf): https://github.com/pecostm32/FNIRSI-1013D-1014D-Hack/tree/main/Manuals [p67, #1669]. Also posted USB.zip, F1C100S_USB_Driver_V1_1.2.zip, linux_uboot_usb.zip, msc_device.zip.
- Spare pin MISO-61-PC2: Atlan wants it as external trigger input via pull-up + optocoupler (<=12 V) [p81 #2023, p82 #2025].
- Hantek DSO2D10 uses the same MCU as the FNIRSI [p76, pcprogrammer, #1889]. Hantek DSO2000 schematics: https://github.com/pecostm32/Hantek_DSO2000
- SPI flash: soldered 25-series (25Q16, 2 MB). Data at 0x1FD000-0x1FD1F3 changes on firmware load, calibration, time set [p77, tokar, #1914]. Alternative firmware uses only the SD card; flash code remains but unused [p77, pcprogrammer, #1913]. Original firmware loads from SPI flash and checks SD for presence and FAT partition [p74, #1828].
- SPI to AD9833 generator: possible in theory; needs extra chip select, disabling flash access, check clock phase/polarity. Two free MCU pins but hard to wire [p77, pcprogrammer, #1911]. Atlan asked (#1910, #1912).

### FPGA variants
- Early batches: Altera EP4CE6 (older units with protruding BNCs); later Anlogic AL3-10; newest appears to be Anlogic EF2L45LG144B (more memory bits, fewer logic cells) [p65, pcprogrammer, #1603; p61 #1518]. https://anlogic.com/en/product/fpga/salelf/salelf2 . Same FPGA programming interface across variants [p61, #1518]. pcprogrammer's firmware still works on the new unit, so design likely unchanged (poster's inference) [p65].
- At least three PCB versions with different FPGAs [p69, Atlan, #1717]. Slight FPGA difference between 1013D and 1014D, not detailed [p90, pcprogrammer, #2246].
- Original Altera: 270 kbit embedded memory; newer Anlogic 496 or 700 kbit [p68, pcprogrammer, #1695].
- FPGA has its own dedicated flash (not configured by MCU); a new bitstream needs dedicated firmware/hardware per revision [p65, pcprogrammer, #1606].
- pcprogrammer reverse-engineered the AL3-10 bitstream; sampling part done, "semi-I2C" part for the special IC skipped [p65, #1603]. Repo: https://github.com/pecostm32/Anlogic_AL3-10_Analyzing ; forum thread https://www.eevblog.com/forum/fpga/reverse-engineering-anlogic-al3_10-fpga/msg3992447/#msg3992447 . Verilog: https://github.com/pecostm32/Anlogic_AL3-10_Analyzing/blob/main/FNIRSI-1013D_FPGA/finrsi_1013D/fnirsi_1013d.v [p77, #1904]. "FPGA explained": https://github.com/pecostm32/FNIRSI-1013D-1014D-Hack/tree/main/FPGA%20explained [p68].
- "Special IC" in original: a data translator, not needed for function [p68 #1683; p70 #1735].
- Adding sample memory by changing FPGA is impractical: needs new ADCs, PCB, input amp, effectively a new scope (Atlan) [p65 #1604].

### ADCs / sampling
- Two interleaved ADCs per channel; at 200 MSa/s ADC clock is 100 MHz [p62, pcprogrammer, #1547]. Original firmware uses one ADC per channel at <=100 MSa/s (reads 1500 samples); second ADC only at 200 MSa/s [p69, #1719].
- ADCs may be B-stock AD9288 clones, possibly rated below 100 MHz and overclocked (pcprogrammer's guess) [p69, #1716]. No part number confirmed in this range.
- No analog/digital ground separation, poor analog supply [p69, pcprogrammer]. Atlan: a 10-bit converter gave nearly the same noise; splitting analog/digital supplies at 8-bit did not help [p69, #1713].
- Real performance (pcprogrammer): ~200 MSa/s and ~30 MHz bandwidth vs claimed 1 GSa/s / 100 MHz; 50 mV/div is software zoom, real minimum is 100 mV/div at 1x [p61 #1518; p68 #1695]. Spec "128 Kpts" false; actual 32768 bits/channel (~4 KB), ~3500 bytes usable [p68, #1695].
- Original firmware draws a calculated perfect sine above ~43-44 MHz [p61 #1518; p67 #1667, video https://youtu.be/SApQPL3pDvc].
- Atlan: AD-to-FPGA lines have different lengths; power/bus layout poor; Lamazoid doubts ferrites help and mentions differential-pair length tuning [p75, #1865-1867].
- "6-bit ADC used over only half its range" limits battery-voltage readout accuracy [p76, pcprogrammer, #1879] (ambiguous; probably battery sense input).
- Input-divider calibration constants expected ~35xxxx/36000/38000/40000 [p83, Atlan, #2059]. 100 mV/div range expects readings 3,000,000-4,000,000 for a 300 mV input [p73, #1805].

### Analog front end / DC offset
- Relays switch ranges; relay on 3 V rail shared with FPGA, ADCs, op-amps, so switching causes pickup and trace shift [p66, Atlan, #1634].
- Op-amp supply: 2.5 V on pcprogrammer's schematic revision; 3.3 V on Atlan's newer/"glass" versions (U17, U13 at 3.3 V, not 2.5 V) [p62 #1545; p82 #2044-2045]. Reference voltage 1.25 V [p82].
- DC offset: FPGA outputs PWM (counter 0-1999 at 50 MHz = 25 kHz carrier, 16-bit pulse width, 0-1999 = 0-100%), filtered to the offset voltage [p82, pcprogrammer, #2044]. Atlan constants HIGH_DC_OFFSET 500, LOW_DC_OFFSET 1200, zero ~860-870, max DC shift 400 [p82 #2043; p89 #2210].
- Original firmware shifts waveform by changing ADC offset voltage; open-source firmware shifts data in software, so the top/bottom clips [p63, pcprogrammer #1563; Sleo #1562]. Hardware DC shift added by Atlan from v0.019c (CH1, positive only), both channels in v0.024c [p68 #1676; p82 #2035].
- Input: >=1.2 MOhm (Sleo estimate), op-amp protection diodes ~10 mA [p62, #1539]; csuhi17 says 1 MOhm [p89 #2202]. 40 V figure is screen-height derived (50 V/div x 8 at 10x), not a hardware limit (Sleo) [p62 #1537]. tokar table (v0021g) of max input at 1x: 5 V/div 10 V; 2.5 V/div 5 V; 1 V/div 1 V; 0.5 V/div 1 V; 0.2 V/div 0.4 V; 0.1 V/div 0.25 V [p74 #1830].
- Firmware never checks ADC overload; range switching only by on-screen fit [p65, Atlan, #1623].
- Soldering defect: two capacitors stacked where one specified on CH1/CH2 boards, caused square-wave mismatch; shields removable [p89, Lelick, #2203].
- CH1 reads 0.15-0.3 V lower than CH2 on several units (kaa, tokar) [p87 #2165-2169]; CH2 has heavier interference [p85 #2108].

### Display, touch, RTC, backlight, power
- Touch: Goodix GT911 (config at 0x8047; code comment mentions GT9157, 800x480) [p67 #1651; p79 #1963/#1966]. Touch panel on bit-banged I2C with own address [p63, pcprogrammer, #1563/#1568]. Guide: https://github.com/pecostm32/FNIRSI-1013D-1014D-Hack/blob/main/IC's/GT911%20Programming%20Guide_v0.1.pdf . Config checksum over first 184 bytes stored at byte 184; last byte must be 0x01 to flag fresh config [p79 #1966; p83 dfw_ee #2058]. Axis-reversal ranges 0x80B7-0x80C4 (Y) and 0x80D5-0x80EE (X), from memory and possibly swapped [p79 #1963]. Writer: https://github.com/pecostm32/STM32F103_GT911_Conf_Writer (Blue Pill; 100 Ohm series, 2K2-4K7 pullups if 5 V MCU). Several display/TP variants exist, each needing different firmware [p79 #1952]. Original firmware wrote TP config to the panel at boot; new SD firmware no longer does (hidden config file instead) [p66 #1648].
- Display bug: PORTD_CFG1_REG should be 0x22272222 (was 0x22222227); LCD_D12 (U1 pin 14) not output, found by _AVP_; pcprogrammer fix untested. CFG0 0x22222227, CFG1 0x22272222, CFG2 0x00222222 [p69, #1708-1710].
- Backlight PWM ~800 Hz from FPGA into a driver rated 1 kHz max; boost coil squeals at 0-50% brightness, original too [p69, pcprogrammer, #1705].
- RTC not fitted in base unit. Optional mod: DS3231 module, remove 2 resistors (0805 pads), solder 4 wires; shares I2C with touch panel [p64 #1584; p65 #1611; p78 #1943]. RTC off by default in menu; temperature display comes from DS3231 over I2C [p83 #2070; p84 #2095; p87 #2155].
- Power: Atlan says regulators spec'd at 4.3 V input are unsuited for a 4.2 V LiPo; suggests 5 V step-up for scope circuits except backlight [p62 #1545; p64 #1593]. Mod: cut 4.2 V feed to the 3 V stabilizer, add 5 V boost, never 5 V to the display inverters; interference remained, extra caps worsened it [p80, Atlan, #1980/#1982]. Alex62: DC-DC at 5 V only increased interference [p64 #1594]. Boost converter in unit is MT3608/SX1308 (tokar) [p89 #2221]; backlight converter candidates AL142/FP6291/MT36291, enable pin 4 [p62 #1549]. 3.3 V LDO MIC29302 failure (red LED, dead display, 1.92 V out) cured by replacing it [p61, illiac4, #1500-1521]. About 1 A drawn at 3.3 V; relays ~0.8 A then 0.55 A at battery [p61].
- Battery indicator jumps due to worn battery connector [p76 #1884-1886]. Original code adjusts time base to compensate for current draw at high sample rates [p75 #1873]. Beeper beeps on original too, louder as battery drops [p69, Atlan, #1704].
- SD card: native SD interface, speed set by card type; intermittent "SD error" 3-5 in 10 boots with custom firmware, never on original; suspected socket or file-system mount failure after loading lots of data [p73 #1823-1824; p74 #1828/#1837/#1838]. _AVP_ saw 10/10 good boots with v0.024j [p85 #2111].
- FNIRSI 1014D differs too much for this firmware [p75 #1870]: https://www.eevblog.com/forum/testgear/new-bench-scope-fnirsi-1014d-7-1gsas/msg4616806/#msg4616806
- USB: stock charge cables are often not data cables; enter the USB connection menu before connecting [p86 #2128/#2141/#2145]. Possible cold joint on USB connector or MCU [p86 #2142].

## 2. FPGA-to-CPU interface (as reported)
- Data port read returns last sample of the selected ADC. Command 0x24 selects CH1 ADC, 0x26 selects CH2 ADC [p68, pcprogrammer #1678/#1683, from memory].
- 0x0D = sample clock divider (32-bit data). Example data 00 00 07 D0 (2000) = 100 kSa/s, used at 100 ms/div [p68]. Verilog: 32-bit counter `sample_rate_divider`, main clock 200 MHz, clk = 200 MHz / ((div+1)*2), rate = clk*2 with both ADCs interleaved; div 0 = 200 MSa/s (100 MHz sample clock), div 1 = 100 MSa/s (50 MHz), div 3 = 25 MHz clock [p77, #1904]. With one ADC rate = clock. Another post: 0x0D "possibly related to post-trigger sample count" (pcprogrammer unsure) [p79 #1970].
- 0x28 = mode select, 0x01 for roll-mode sampling (unclear if needed per read) [p68].
- 0x0E = `time_base_set`; Atlan writes `timebase_settings[]` (indices 0-23, 200 ms/div to 5 ns/div) via fpga_set_time_base() [p79, #1968]. Table values (e.g. 1800, 2500 for 200 ms-1 ms/div) are sample counts, probably legacy [p79 #1967].
- 0x01 with data 0x01 resets sampling system and `time_base_cnt` [p79 #1972]. `time_base_cnt` counts on the sample write clock; when it exceeds time_base_set, `time_base_timeout` is set; auto mode then stops sampling, normal mode stops only after trigger plus enough samples. Trigger can be bypassed to collect a fixed sample count only in Auto mode [p79 #1972; p80 #1978].
- Roll data reads: 10 bytes per read averaged by software; repeat reads may return the same sample; no timed-capture support in FPGA [p68 #1678/#1681].
- Sample buffer: 4096 samples/channel in FPGA, software reads ~3000 (~750 per screen, ~4 screens) [p68 #1690/#1689]. FPGA does not use all its memory; odd trigger-related numbers [p68 #1692]. Trigger sits mid-memory (~1500 of 3000 samples for signal) [p84 Atlan #2085].
- Trigger: mostly in FPGA, "basically crap" (pcprogrammer); detection runs on captured samples so limited by sample rate and misses narrow pulses at slow timebases [p64, #1576]. Hardware trigger does not report edge direction in long mode; Atlan moved edge detection to software [p77 #1915].
- v0.023s stops waiting for FPGA conversion, so display no longer freezes [p78 #1945].
- Original firmware filtering is heavy and inefficient (pcprogrammer); Atlan prefers raw data [p64 #1576/#1577; p62 #1547].

## 3. Firmware: layout, boot, tools
- Boot chain: F1C100s mask ROM checks SD (before SPI flash) for an eGON header at SD sector 16 (1 KB block 8, hence `dd ... bs=1024 seek=8`), loads to SRAM; `fnirsi_1013d_sd_card_bootloader` initialises DRAM, loads next program (PROGRAM_START_SECTOR 48) and display config (DISPLAY_CONFIG_SECTOR 710); `fnirsi_1013d_startup_screen` (PECOs SCOPE splash) at 0x81C00000; scope program from sector 92 to 0x80000000 [p70 #1735/#1741; p71 #1756; p82 #2028]. Some posts say "sector 8" loosely (1 KB blocks). Header bytes: 06 00 00 EA 65 47 4F 4E 2E 42 54 30 F7 6F 69 AE 00 3A 00 00 53 50 4C 02 00 00 00 00 00 00 00 00 [p82 #2028].
- Remove alt firmware: zero first 32+ bytes of sector 16, or write 00h.bin over sectors 16-2047 [p70 #1728/#1730/#1735; p90 #2248]. Need >=1 MB free before first partition; MBR not GPT [p80 #1995; p76 #1891].
- SD layout: settings from sector 700 (SETTINGS_SECTOR; ~5 KB before display config), display config 710; program <~300 KB, beyond ~346 KB overwrites sector 700; 1 MB reserved [p70 #1731/#1741]. Atlan later moved calibration data to sector 709 [p80 #1992], input calibration to 708 [p87 #2164]. Settings version constants SETTING_SECTOR_VERSION_HIGH 0x0100 / LOW 0x0002; mismatch loads defaults [p75 #1856/#1859/#1863]. Proposed offsets (16-bit units): CH1 10, CH2 40, trigger 70, other 100, cursor 130, measurement 160, calibration 200, input cal 230 (14x32-bit) [p77 #1901].
- Build: Linux only, gcc-arm-none-eabi; no top-level makefile, each project has its own (NetBeans nbproject); order sd_card_bootloader -> startup_screen -> scope; makefiles copy outputs between projects and merge into one bin [p81 #2012/#2018/#2024]. `mksunxi` fixes eGON checksum; `flashfilepacker` (pcprogrammer, https://github.com/pecostm32/FNIRSI-1013D-1014D-Hack/tree/main/Test%20code/flashfilepacker) merges bins, e.g. `./flashfilepacker -o dist/Debug/GNU_ARM-Linux/fnirsi_1013d.bin -i dist/Debug/GNU_ARM-Linux/fnirsi_1013d_scope.bin -l 0x9800` [p82 #2034/#2037]. Offset 0x9800 into the image maps to sector 92 if the image starts at byte 8192 (my inference). Scope bin ~258,048 B; full bin ~297,032 B.
- Atlan builds on Ubuntu CLI; NetBeans + C compiler on Windows untested/unsolved; https://github.com/tsnsoft/NetBeansForCPP [p81 #2010; p89 #2204-2207]. Atlan's makefiles are hand-edited; pcprogrammer repo preferred [p81 #2010]. Atlan: v0.019d-0.020e "poorly translated from C to BIN" [p71 #1758].
- Emulator: pcprogrammer's PC emulator (Linux/X11; someone hooked QEMU): https://github.com/pecostm32/FNIRSI-1013D-1014D-Hack/tree/main/Test%20code/Scope_Signal_Handling_Development [p71 #1750]. Original-code port: https://github.com/pecostm32/FNIRSI-1013D-1014D-Hack/tree/main/Test%20code/fnirsi_1013d_original_scope (`scope_do_auto_setup` in scope_functions.c, "not fully finished"; `scope_get_long_timebase_data` lead) [p73 #1818/#1820]. pcprogrammer used a Ghidra archive to find functions for the generator [p90 #2246]. Atlan has no simulator or debugger [p70 #1749].
- Backup/restore: `fnirsi_1013d_fwb.bin` flash backup tool writes FNIRSI_1013D_full_flash_backup.bin, FNIRSI_1013D_tp_config.bin, FWB_FSI-1013.bin to SD; https://github.com/pecostm32/FNIRSI-1013D-1014D-Hack/tree/main/Test%20code/fnirsi_1013d_firmware_backup [p78 #1949; p90 #2231]. SPI flash can also be read with a CH341 programmer; tokar's matched [p90 #2230-2231]. Original binaries: https://github.com/pecostm32/FNIRSI-1013D-1014D-Hack/tree/main/Binaries/Original%20files [p88 #2178]. `fnirsi_1013d_startup_with_fel.bin` runs from SD in RAM for FEL access [p79 #1962]. Backup procedure: dd of whole SD, restore with `dd if=fnirsi_1013d_fwb.bin of=/dev/sdc bs=1024 seek=8` (only 8 GB cards tested) [p78 #1949].
- Loader (Sleo, Windows, Delphi, admin): load/verify/delete firmware over USB (scope in USB menu enumerates as F1C100S device) or SD in card reader; edit LCD/TP config sector; "Clear" zeros first byte of firmware sector [p61 #1524; p62 #1529; p70 #1736/#1746; p86 #2125]. Atlan video: https://youtu.be/LVI45oJHTK8
- Repos: pcprogrammer https://github.com/pecostm32/FNIRSI_1013D_Firmware ; Atlan https://github.com/Atlan4/Fnirsi1013D (guide .../tree/main/Guide%20to%20firmware ; TP write .../blob/main/TP%20write%20config%20v0.023x.zip). Atlan's manual (.doc, v0.023x/v0.024c) attached on p80/p82; "page 82 of thread".
- Original-firmware bugs per pcprogrammer: USB mass storage hangs on disconnect, wrong time/div when zooming a stopped trace, synthetic sine >44 MHz [p61 #1518]. Open-source v0.005 lacked FFT and ROLL (50 s-20 ms) [p61].
- Version milestones (Atlan unless noted): v0.006 RMS fix by Michal Derkacz (ziutek) [p61 #1522]; v1.007-1.013 (likely 0.007-0.013) calibration, trigger colours, thumbnails; 0.015 trigger fixed at sample centre, red/green marker; 0.016g RTC; 0.017 trigger-point search only above 3.5 MHz; 0.018 DC offset; 0.019b Windows USB mount ~63 s -> ~6 s; 0.019c hardware DC shift and calibration menu; 0.019e scroll through full memory; 0.020 input-divider calibration; 0.021 calibration reset; 0.022 autosetup; 0.023 long time base (100 ms-50 s/div), WAV RMS; 0.024 TP4, DC offset both channels; 0.024o dividers 0.5-1000 and V/A mode; 0.024v old/new autosetup [p63-p89]. 1.00x builds still mount slowly on Windows (>1 min) [p90 #2233]; Khial runs "V1.012" [p90 #2232].
- USB mass storage: Linux mounts <1 s; Windows slow due to unimplemented mass-storage reset / missing capacity value seen in Wireshark capture [p67 #1658/#1664]; fixed in v0.019b/d.
- BMP screenshots are 768070 B with a 56-byte DIB header (16 bpp colour masks, chosen on purpose); Pillow/Firefox reject; one-byte change at 0x0E to 40 fixes; Atlan v0.021a uses 40 [p71 #1753/#1757; p72 #1781-1785].
- Original autoset does not detect AC/DC, uses polarity+level in DC, lets channels use full 8 divisions; alt firmware limits to 4.5 div/channel [p73 #1819/#1822].

## 4. Calibration (alt firmware)
- Baseline calibration (find zero) with nothing connected; input-divider calibration steps 0.3, 0.6, 1.5, 3.0, 7.5, 15 V for 100 mV...5 V/div using a noise-free battery/linear source (SMPS 200+ mV noise too high) [p73 #1807; p88 #2194]. Data written to SD on power-off; reset via USB menu centre click; error shows 4000001 [p72 #1792/#1797; p73 #1810]. bffargo result: 0.30->0.3006, 0.60->0.6004, 1.50->1.503, 3.00->3.001, 7.5->7.499, 15.0->14.97 V [p73 #1807]. DC calibration targets Vavg [p85 #2115].
- Per-unit variance: input sections differ slightly [p68 #1676]. Offset coefficient is nonlinear in DC mode [p87 #2163].

## 5. Measurements and limitations
- Interference rises at 200 MSa/s (ADC clocked 100 MHz) [p62 #1546-1547]; video https://youtu.be/QJjuiEPd3dA
- Trigger jitter blamed on input/ADC design and PSU interference [p63 #1574]; original hides this with heavy filtering.
- St_dsk (Altera unit): output voltage frequency-dependent above 4 MHz [p69 #1701]. Sleo: 1-25 MHz sine on v0.005/0.006/0.019b, no spikes, only shaky line [p68 #1680]. OneDiy saw spike on HF sine [p67 #1666].
- With input open, voltmeter offset shrinks as sensitivity rises (tokar) [p66 #1633]. Original ignores 2-3 bit changes near zero [p66 #1636].
- Long mode vs normal mode ~400 mV offset (LunaC) [p81 #2016]. Zero-level ripple at <=50 kSa / 2 ms [p80 #1979].
- Screen density: alt firmware 10x200 points vs theoretical 4x700 [p68 #1694].
- Long mode: 3000 samples/channel; time base 100 ms-50 s/div by lowering sample rate [p77-78].
- Autoset/frequency counter fails below ~350 mVrms (v0.024) [p85 #2120].
- Mains measurement: pcprogrammer advises against [p62 #1535].

## 6. Ideas and wishes not implemented (as of p90)
- FFT with X cursor [p67 #1657]; roll mode via timer interrupt per time/div [p68 #1678]; dual zoom view [p68 #1692]; averaging/dot/sync display [p61 #1512]; external trigger input via optocoupler [p81]; AD9833 BNC generator over SPI, I2C-controlled input amp gain, new PCB/new FPGA program [p77 #1918/#1920-1921; p84 #2085]; CH32V003 helper project [p83 #2053]; current-probe mA/A mode and dividers 0.5-1000 (partly done in v0.024o) [p84-86]; DC shift menu option, RESTORE calibration button, firmware version on calibration screen [p90]; battery percentage/voltage readout [p75-76]; beam-zero reset [p88 #2187]; automotive presets [p86 #2148]; pcprogrammer porting Atlan's changes to 1014D code and writing a generator [p90 #2246]; changing sample rate in single mode [p90 #2241-2244].

## 7. Open questions / contradictions
- FPGA per revision: p69 St_dsk calls his "first version, Altera" while signature links name AL3-10; progression Altera -> AL3-10 -> EF2L45 from p61/p65. Board markings per revision not given.
- SoC: F1C100s everywhere; one F1C200s manual link [p82]. Compatibility unverified.
- Sample memory: "4096/channel, software reads ~3000" [p68 #1690] vs "32768 bits ~4 KB, ~3500 usable" [p68 #1695] vs Atlan "3000 bytes". Unexplained.
- Divider formula (p77): div 3 gives 25 MHz clock (50 MSa/s with two ADCs). Check against the Verilog.
- ADC part number still unidentified (AD9288 clone is speculation) [p69].
- Op-amp rail 2.5 V vs 3.3 V depends on board revision [p82].
- Sector 8 vs 16: 1 KB-block vs 512-byte-sector wording; byte offset 8192 is the same.
- Settings/calibration sector moved 700 -> 709 -> 708 across Atlan versions; confirm in latest source.
- Bandwidth/sample-rate figures are pcprogrammer's estimates; no formal sweep in these pages.
- Digests are secondary summaries; hex values and quotes need verification at source.
