# EEVblog thread "FNIRSI-1013D 100MHz tablet oscilloscope", pages 121-155

Posts #3000-#3872, 2025-05-04 to 2026-10-16. Page 155 is the last page at fetch time (#3872). Citations are [pN, poster]. Data came from WebFetch summaries, so exact figures should be re-verified against the source before relying on them.
Thread URL: https://www.eevblog.com/forum/testgear/fnirsi-1013d-100mhz-tablet-oscilloscope/ (page N = /(N-1)*25/)

Naming used by posters: "PECO"/"Peco" = pcprogrammer's open-source firmware lineage; "Atlan & PECO" = the community firmware Atlan continues. "FNIRSI/OEM" = stock.

## 1. Main actors and state of the project
- pcprogrammer (Netherlands): wrote the original open-source firmware and did the hardware RE. Only occasional hardware advice in this slice [p123, p137, p140-p142, p148, p150, p154].
- Atlan (Slovakia): sole active firmware plus FPGA developer in this slice. Maintains GitHub Atlan4/Fnirsi1013D. Firmware lines: 0.26x (to Aug 2025), 0.27x (Nov 2025), then 1.00x with custom FPGA (Dec 2025 onward).
- ObsoleteDIY (Slovakia): from Jan 2026 modifies the EF2L45 FPGA Verilog (zaklad.v) and tests trigger logic. Atlan said he has worked on the FPGA about a year and a collaborator recently joined [p146, Atlan #3646]; "one person works on the FPGA" [p153, Atlan #3808].
- End state (p155, Oct 2026): DSO firmware v1.00q2 (May 2026) is the latest documented; FPGA versions 2014/2015 (EF2), 3014/3015 (AL3), 2017/3017 mentioned for dual-channel trigger. Hold-off, FFT (disabled in 1.00 line), roll mode not implemented.

## 2. Hardware
### 2.1 FPGA variants and how to identify
- Three tablet PCB types: one AL3 and two EF2 variants [p136, Atlan #3394]. New units have EF2 [p136, Atlan #3396]. Switch to EF2 attributed to AL3 end of life and price [p137, pcprogrammer #3400; his attribution].
- First-draft board used Altera/Intel Cyclone IV; BNCs protrude from the top (not recessed). Altera boards keep FPGA markings; pcprogrammer owns a Cyclone IV unit [p137 #3400; p141 #3505-#3506].
- Altera part seen: EP4CE6E22C8N with W25P16V config flash (dump posted) [p143, St_dsk #3555/#3558]. AL3 and EP4CE6 are pin-compatible; EP4CE10 would give more sample memory [p143, Atlan #3557].
- Anlogic AL3-10 ("AL3a10"): config in external SPI flash, FPGA image 1 MB (Fnirsi FPGA.bin 1024 kB). Flash chips reported: 25VQ16ASIG (stock, mishail71) [p138 #3433; p139 #3474], ZB25VQ80 dump [p139 Atlan #3451], Winbond 25Q80DV on ded's board with connector pinout differing from forum's [p142 ded #3542], 25VQ80 per Atlan [p139 #3473]. A 25VQ32 worked as replacement where 25VQ16 failed [p139 #3474].
- Anlogic EF2L45 (EF2L45LG144B; TD shows "EF2L45B"): internal flash, programmed only via JTAG [p130 Atlan #3245; p142 #3532; p155 ObsoleteDIY #3851]. Note: Atlan's p130 text says "the EF2 has firmware in its flash" and the AL3 in external SPI.
- AL3 board has a 6-pin FPGA-flash header and a 4-pin header Atlan calls an "encryption circuit"; pcprogrammer says the board label "JTAG" is wrong and the header is I2C to a special anti-clone IC; Moondeck/Atlan identify N24C08 (bitstream logging chip, unnecessary) [p136 #3394; p148 pcprogrammer #3690; p149 #3712, #3714].
- Identification tips: FPGA marking may be sanded off and replaced by a number (e.g. "017325203512"); identify AL3 by layout (SPI flash beside it) or by reading flash and checking the bitstream header ID [p148 Etrius #3682; p141 pcprogrammer #3506; p142 #3543]. Some units have no FPGA flash at all (EF2) [p137 #3416]. Another layout without recessed BNCs has SPI pins shuffled [p149 Atlan #3714].
- Stock unit loops at startup if FPGA absent; FPGA also turns on the backlight; MCU checks the FPGA at startup [p139, Atlan #3455].

### 2.2 Other ICs and board facts
- MCU Allwinner F1C100s; 2 MB SPI NOR holds stock firmware; FEL mode via xfel (`xfel.exe spinor read 0 2097152 flash_dump.bin`), asked for a 1014D [p151, joker75 #3753]. pcprogrammer pointed to the 1014D thread dump: https://www.eevblog.com/forum/testgear/new-bench-scope-fnirsi-1014d-7-1gsas/msg4102147/#msg4102147 [p151 #3754].
- ADC: MXT2088 dual ADC; each channel uses two ADC halves (interleaved); there is no switch to route one channel to both ADCs, so single-channel mode is 200 MS/s [p122, pcprogrammer #3033]. Atlan: "ADC takes 2 samples of 8 bits"; each FPGA memory address holds 16 bits (ADC A and B) [p147 #3650; p148 #3695].
- Sample memory: EF2 6144 addresses x2 samples = ~12,000; AL3/EP4CE6 ~16k (program limits to 15k) [p146 #3631; p147 #3650]. DSO v1.00p2 fixed counts EF2 8191, AL3 6143 [p150 #3738] (conflicts, see section 8). DSO assumed circular memory of 2x8k [p146 #3644].
- Backlight driver: MT9284-B11G8 (PWM enable 20 kHz to 1 MHz); parts marked B11G8a/B11G8Q; ded thinks PT4103B23F [p142 Atlan #3537, rozwell #3538, ded #3540]. pcprogrammer suggests current-set via shunt instead of toggling EN [p142 #3536]. Atlan's dimming-frequency change (to stop coil whine) broke brightness on EP4CE6 builds (fixed) and AL3 [p143 #3560-#3568, engineer.r152 #3569]. Hardware workaround: 3.3 V via resistor to backlight IC EN [p143 St_dsk #3573].
- Voltage reference: 1.25 V shunt reference ("IC432", TL432/TLV431) forms the virtual ground for input stage; "IC431" handles battery voltage. A 2.5 V TL432 variant caused baseline-calibration Fail-1; replacing with 1.25 V TLV431 fixed it on two units [p132 Sorgelig #3280-#3295; p146 firefly-10 #3641, #3655; Atlan #3638, #3642]. Sorgelig: schematic should say TLV431 [p132 #3284]. Checks: SD-card metal case vs BNC GND, 100 ohm resistor BNC to Vref1 [p130 Atlan #3232; p146 #3638].
- Fault-find voltages (missing trace): Analog_ADC_Input 1.24 V, ADC pins 5/6/7 1.2 V, ADC_Offset after two filter elements 1.86 V; ADC pin 2 input, pin 3 via C28 [p130 User1914 #3238; p132 Atlan #3279]. Scorched BNC foil and CH2 +39.2 V after probable ground measurement [p132 #3291].
- Regulator MIC29302A (post also says MIC2302/MIC23902): pin 2 in (to 15 V), pin 4 out to MCU (believed 3.3 V) [p130 umberto2 #3230; p131 Sorgelig #3258]. Touchpad erratic with charger connected or when measuring 200 V impulses [p130 umberto2 #3227].
- Battery: two TP4056 chargers with 3k and 1k set resistors [p127 King_E #3165]. Low battery moves the trace; step-up converter fix, ideally with EN tied to MIC23902 pin 1 so it powers off [p131 Atlan #3256, Sorgelig #3258-#3259; p141 ded #3508].
- RTC: DS3231 module on SDA/SCL pads; coin cell can die in about a year; boojum added external 2032 holder [p137 #3405]. Firmware 0.25 saved RTC, 0.27u did not for gadak222 [p136 #3390]. Updater clock shows build time only [p142 Atlan #3545].
- SD card: factory no-name 1/4/8 GB cards often bad ("SD Error", "boot sector not found") [p128 rampageagain #3193; p130 Catlish #3228]. Need hidden sectors: firmware needs 688, some cards have 31; use Loader 1.1.3 "Resize" then FAT32 [p129 Sleo #3216]. v0.26v3 uses 670 sectors, 708 already filled [p126 Atlan #3141]; sectors 709/710 hold calibration/display config; 708-710 erased by ALL image [p131 #3260; p133 #3318]. 2 MB dedicated space required [p151 #3756].
- Touch/display config: Loader "LCD/TP Config" tab, shifted_display_config_sector.bin [p124 rostik08 #3084-#3088; p130 umberto2 #3227]. Stock display settings in 0x04xxxx region (0x04F350 / 0x4E918) [p155 cats-shadow #3872 quoting pcprogrammer 2024].
- ADC channel mismatch case: CH2 about 9% offset between ADC A and B plus sawtooth; Atlan suggested removing the ADC CH2 input resistor and feeding CH1 through 220R as a test [p151 waterstarrunner #3758-#3759].
- DPOS350 reference: eight 125 MS/s ADCs; pcprogrammer calls it bad design vs Hantek DSO2000 single ADC [p122 #3035].
- 1013D has no function-generator hardware; 1014D has one [p124 pcprogrammer #3075]. Custom-FPGA square generator is a logic output, use x10 probe at high frequency [p140 Atlan #3494].
- Noise: 6-9 mV at 1x after input calibration (24-29 mV before on CH2) [p121 mehdifat #3003, #3020]; vertical readout resolution scales with V/div (1 px ~1 mV at 50 mV/div) [p121 bffargo #3021, Atlan #3024]. Frequency resolution 32.7 Hz for 32768 Hz crystal, horizontal jitter, sine asymmetry +510/-470 mV [p140 TriNitron #3479]; he suggests femto-clock replacement [p141 #3501] (inference, untested).
- Off-topic: FNIRSI 2D15P auto 20 MHz bandwidth limit below 2^22 Hz [p155 Serhii #3871]. A relabel "UTRNIX ADS1013D" exists [p155 LC2047 #3868].

## 3. FPGA work
### 3.1 Versioning scheme
- fpga_get_version(): 0x1432 FNIRSI OEM (1), 0x1532 PECO AL3 (2), 0x1632 PECO EF2 (3) [p147 Atlan #3671].
- Scheme from DSO 1.00o9/p0: 0xZYYY, Z=1 OEM, 2 EF2, 3 AL3, 4 EP4 (Altera); YYY decimal digits stored in hex (0x3001 = AL3 fw 001) [p149 #3717, #3720; p150 #3727].
- DSO p0/p1+ needs new-scheme FPGA (EF2L 0x2000, AL3 0x3000); DSO <= o8 pairs with 0x1532/0x1632 [p150 ObsoleteDIY #3731-#3733]. Atlan: use matched complete packages; GitHub AL3 is behind [p149 #3705; p152 #3786].
- Diagnostic menu shows FPGA type, version, limitations [p140 #3487; p147 #3654].

### 3.2 Capabilities of custom FPGA vs stock
- Stock FNIRSI FPGA: 3000 samples, trigger 50% only, generator not working [p139 Atlan #3462].
- Atlan AL3 build: ~16,000 samples (default 16300, usable ~16380), trigger anywhere in memory (practically 25/50/75%), generator 0-5 MHz frequency (earlier said 1 Hz to 25 MHz) with 0-100% duty, fixed trigger points 750/1500/2250 at 3000 samples, sample count over serial `f9000` [p136 #3387; p138 #3442; p140 #3498; p141 #3517].
- EF2L45: smaller, max ~12,000 samples [p136 #3387; p143 #3554; p146 #3631].
- Fixed issues: auto-mode with no trigger kept reading memory after write stop (random data) [p138 #3441, #3444]; frequency/time-+ errors above 6000 samples fixed by overflow fix and v1.00n8 [p140 #3498; p141 #3515]; generator misbehaves with +duty <10% above 1 MHz (4 MHz/10% outputs 2 MHz) [p140 corgon via Felipe #3496].
- Trigger is entirely in the FPGA; DSO sends trigger value. Hold-off is not programmed in either FPGA [p150 Atlan #3728; p153 #3812]. ObsoleteDIY offers to implement hold function after dual-channel trigger testing [p153 #3822].
- 200 MS/s timing: EF2L issue fixed by PLL phase shift 90 to 45 [p151 ObsoleteDIY #3750]; "0x1832" debug build fixed 200 MHz timing and memory overflow [p146 #3643]; AL3 3015 much better at 200 MS/s [p152 Felipe #3783]; 2014/3014 had glitches at 200 Msps [p152 ObsoleteDIY #3780].
- ObsoleteDIY analysis of EF2L45 zaklad.v: FPGA detects edges only; writes circular memory from address 0 after reset; trigger enabled once stored samples exceed pretrigger_samples; trigger_address stored on edge; writing stops after posttrigger_samples or when time_base_cnt expires. Changes: MEM_END_ADDR/addr_delta (6143/6144), sample_clock_enable, addr_delta overflow fix (line 457 `((trigger_read_address + 6144) - trigger_address)` to `((6144 - trigger_address) + trigger_read_address)`), trigger_address on time_base_out, dual-channel A+B edge detection with arming (rise_edgeA/B, fall_edgeA/B, armi_*), arming reduced 4 to 1 samples, 0x2008 better, 0x2009 compares AB/BA combos [p146-p150, e.g. #3645, #3659, #3666, #3680, #3699, #3704, #3725, #3729, #3748].
- Auto-trigger: DSO uses fixed sample count; OEM table `timebase_settings[35]`: 5000 for 200 ms to 500 us/div; 8192 at 100 us; 5596 at 50 us; 9692 at 20 us; 21980 at 10 and 5 us; 83420 at 2 us; 206300 at 1 us; 411100 at 500 ns and faster. For non-OEM FPGA the duration is samplecount scaled by timebase index (x1,x2,x4,x6,x10,x20,x40,x80,x160). A 100 ms range full transfer takes ~1.5 s [p147 #3667, #3671; p148 #3692].
- DSO to FPGA command bytes seen: 0x0B (pretrigger samples = settriggerpoint/2), 0x0C (total samples = totalsamples/2), 0x0E (short time base), 0x14 (prepare transfer, then read trigger address masked 0x1FFF), 0x1F (read samples from translated trigger point) [p147 #3671; p148 #3692]. Atlan posted "FPGA commands.txt" (8.6 kB) with ALL18 [p151 #3772].
- Trigger-address correction for EF2 (circular 6144): if data < settriggerpoint/2 then data = 6143 - settriggerpoint/2 + data, else data - settriggerpoint/2; constant 6143 vs 6144 unresolved; old code used +5393 [p148 #3694-#3697; p149 #3716].
- Trigger limits MIN 500 / MAX setsamples-500; ObsoleteDIY suggests 750 [p150 #3727, #3729].
- DSO display: modes DOTS, LINEAR (skips samples), PRO, PEAK; Trace menu; screen shows 723 points so 3000+ samples are decimated [p139 #3462, #3467; p146 #3631].

### 3.3 Programming methods and procedures
- AL3 modification (Atlan, Nov 30 2025): confirm AL3; install Bootloader 0.8, updater 0.02t, scope 1.00l6; Updater DOWNLOAD (SPI backup FLASH) to keep stock firmware image; desolder uP SPI flash (near SD card, 25Q16/25VQ16); wire four leads (ignore green/white-green) from the MCU flash pads/header to the FPGA flash connector; DOWNLOAD (FPGA FLASH) saves original to firmware_FNIRSI_FPGA; copy new FPGA.bin to "firmware Atlan & PECO FPGA"; unmount; UPLOAD in FIRMWARE PECO section [p137 #3416; p141 #3510]. pcprogrammer: hardware mod required, FPGA flash cannot be written otherwise; wire FPGA flash to MCU flash pads via the two empty header footprints, desolder only MCU flash; original firmware no longer works [p148 #3687, #3690]. Alternative: put a cloned 25Q80 on the board [p148 Etrius #3691].
- A bitstream of 2 MB indicates wrong source; stock FPGA image is 1 MB [p138 #3428-#3430].
- Updater v0.02v only uploads FPGA SPI image (stock or alternative) [p139 #3465]. Bootloader v0.7 checks FPGA presence/version; v0.8 skips version check and waits briefly (needed for custom FPGA); v0.7 will not work with the custom FPGA [p141 #3514; p145 #3622]. Atlan: "at power-on the FPGA loads from SPI flash while the MCU loads bootloader from SD" [p144 #3580].
- Bricking: mishail71 hit UPLOAD CPI BACKUP Flash by mistake; CH341A_C programmer with 3.3 V; check flash protection bits and pin 1 orientation; 3V regulator failure possible [p139 #3450-#3457]. engineer.r152 bricked after AL3 FPGA 3014, restored by CH341 [p144 #3576-#3581; p152 #3785-#3787]. Romeo53 bricked EF2 then recovered via TD "Erase Flash", select .bit, Program Flash mode (not JTAG), Run [p154 #3829-#3831].
- CH341A caution: may drive 5 V; read in-circuit with the scope powering the flash, not the programmer [p141 pcprogrammer #3513].
- EF2L45 JTAG programmer: STM32F103C8T6 Blue Pill with anlogic-usbjtag firmware (https://github.com/AnlogicInfo/anlogic-usbjtag; Atlan copy in Atlan4/Fnirsi1013D "Verzia 1.xxx/FPGA for 1.xxx/FPGA EF2/PROGRAMER anlogic-usbjtag-master"; pcprogrammer: https://github.com/pecostm32/Lichee_Nano/tree/main/Hardware/Anlogic_JTAG_Programmer incl. Schematic_Anlogic_jtag_programmer_2022-01-23.pdf and udev rules 99-Anlogic-programmer.rules). Wire a 6-pin header near EF2; 3.3 V from the scope (STM cannot supply ~1.3 A); use resistors or 74LVC125 buffers; TCK needs all three connections via resistor; connect cable unpowered then power scope; need bootloader 0.8; flash flash.bin to STM with ST-Link Utility [p144 #3584; p145 #3604, #3612; p154 #3838, #3840]. Flash_ok.zip fix (p155 Alex327 #3864).
- Software: Anlogic TD. 4.6.8 SP1 works for EF2L45; 5.0.3/5.0.4 differ (timing and bit content differ); license expiry Jan 2026; pcprogrammer shared TD 5.0 zip https://drive.google.com/file/d/12t6vhkghCVNoR0sE3o9tCnyUZA6NMUU8/view?usp=drive_link [p140 Atlan #3480; p144 #3598; p145 #3615-#3617; p146 #3640]. Development on Ubuntu; Windows needs driver-signature enforcement disabled or TD's own driver (C:\Anlogic\TD5.0.29524\driver\) [p154 #3841; p155 #3851-#3858]. TD detect states: "No hardware" = USB driver; "USB cable connected" = wiring/power; "EF2L45B" = OK [p155 #3851]. Errors: PRG-9507/9512/9523, GUI-8702, PRG-9525, PRG-9028 [p145 #3615; p154 #3828; p155 #3862]. Atlan's project "FPGA EF2L45 ubuntu.zip" (zaklad.v, zaklad.sdc) [p145 #3609; p147 #3652]. EF2 flash read lengths 162011/162012 [p145 #3607]; ObsoleteDIY OEM dumps my162011.hex/my162012.hex [p145 #3613]; restoring OEM works [p145 #3617].
- Bitstream formats: TD .bit has header + CRC; .bin strips header; read-back "2.hex" has 16-byte header; pcprogrammer's converter Anlogic_flash_to_bit_converter.c in https://github.com/pecostm32/Anlogic_AL3-10_Analyzing (EF2 header lines 76/153/386; CRC32 polynomial unknown); bitstream doc https://github.com/mmicko/prjtang/blob/master/docs/architecture/bitstream_format.rst [p144 #3590-#3599; p145 #3601-#3602]. Atlan's conversion failed on CRC mismatch [p144 #3594].
- Atlan: EF2 SPI pins could theoretically upload config, not pursued [p153 #3820]. First EF2 upload success Jan 5 2026 [p143 #3553]. EP4CE6 build fnirsi_1013D_EP4CE6.bin works, brightness fixed [p143-p144 #3560-#3595].

## 4. Firmware
### 4.1 Architecture and tooling (new split from Oct 2025)
- Components: Bootloader (0.06 in Oct 2025; 0.7/0.8 later), Updater (0.01t, 0.01u/y, 0.02f/g/l/n/r/t/u/v), Scope/DSO firmware. Images updater.bmp and scope.bmp go in SD root; folder "firmware Atlan & PECO" (+ "... FPGA"); stock backups in firmware_FNIRSI ("Fnirsi 1013D.bin" English), firmware_FNIRSI_BACKUP ("Fnirsi backup.bin", full 2 MB flash), firmware_FNIRSI_FPGA [p133 #3307-#3318; p134 #3336; p135 #3358, #3409; p139 #3465].
- Updater: backup full SPI flash to SD, upload stock firmware (no checks), upload scope/updater, upload/download calibration (data.cal) and TP&screen.bin, SPI flash presence check and write verify (0.02n), USB On/Off icons [p134 #3326-#3336; p136 #3380; p137 #3403]. Boot selection (PECO / FEL / FNIRSI) option; stock firmware boots from and saves settings to SPI flash; alternative boots from and saves to SD [p135 Atlan #3356].
- Migration: load 0.26v via Loader 1.1.3, enable boot menu, upload fnirsi_1013d_migration.bin ("migrationV2.bin"), then use updater to load scope (scopev27.bin) [p134 #3335, #3340; p135 #3372]. New users: fnirsi_1013d-ALL.bin (672 kB) erases sectors 708-710 [p133 #3318]. Linux/macOS: `dd if=<image> of=/dev/sdX bs=1024 seek=8` onto unmounted scope USB disk after enabling boot menu; check device name first [p130 RolfNoot #3237; p134 #3339-#3340]. Bare cards: see msg3942868 in earlier pages.
- Loader (Windows) loader_v1.1.3.zip: BOOT tab, LCD/TP Config, calibration backup/restore, Resize hidden sectors [p124 TA4DIL #3090; p129 Sleo #3216; p135 #3353]. Also "FNIRSI 1013D Firmware Upload Methods.pdf" [p155 #3868].
- Guide: https://github.com/Atlan4/Fnirsi1013D/tree/main/Guide%20to%20firmware (subfolders "Frst use (install) firmware" and "Transition from version (0.0x - 0.26v7) to new botloader, updater, scope/firmware_Atlan_Peco"). FPGA files: https://github.com/Atlan4/Fnirsi1013D/tree/main/Verzia%201.xxx/FPGA%20for%201.xxx [p143 #3567].
- USB serial (CDC) emulation added Nov 2025; PC app "FNIRSI1013D-APP-beta10" by Hiban (made for DSO2512G), inefficient (~1 MB needless transfer), demo only; enable USB serial mode, power cycle, use "scope v0.27s.bin.hex" [p135 #3374; p136 #3375-#3376; p138 #3445; p148 #3676]. No command set documented beyond the dev-mode console below.
- Dev-mode serial console: enable serial port and dev mode, power cycle, any baud (e.g. 9600) in Arduino/Linux terminal; `f3000` returns/sets sample count, `f9000` sets 9000; wrong values can write unknown memory [p138 Atlan #3439, #3442].
- Calibration: baseline calibration (Menu > System Settings > Baseline Calibration, probes unplugged); then input-divider voltage calibration with DC 300 mV, 600 mV, 1.5 V, 3 V, 7.5 V, 15 V (two 9 V batteries plus 50-100k multiturn pot), both probes x1 (scope forces x1); then DC-shift calibration (3 parameters; only in DC coupling with offset; applied after Autoset) [p123 TA4DIL #3063; p135 #3361; p153 Russell #3814]. BASE fails: FAIL1 average ADC1 raw outside 125-131; FAIL2 compensation sum split across ADCs outside +-20; calibration data in sectors 709/710 [p131 Atlan #3260, #3262]. Backup via Updater "Upload Calibration Data" to SD or Loader [p141 #3527; p153 #3814]. Videos: https://youtu.be/81gYnOh9r8s, https://youtu.be/U086LOFFgn0, https://youtu.be/KzT5EDAOOlQ.
- Diagnostics screen: orange button loads basic default values; red button resets calibration; DEV mode not for users [p131 #3250; p133 #3304; p139 #3465].

### 4.2 Version history (Atlan unless noted)
| Version | Date | Notes |
|---|---|---|
| v0.26r6/r7 | May 5-6 2025 | REF load/save fixes, REF colours, date fix [p121] |
| v0.26s, s1, s2, s3 | May 11 - Jul 2025 | REF buttons; fw version in wave file; timestamp; BMP tag [p122-p125] |
| v0.26u, u6, u8, u9 | Jul 6-9 2025 | 4 then 8 refs, descriptions [p125-p126] |
| v0.26v1-v7 | Jul 9 - Aug 6 2025 | touch-move refs; "load ref to startup"; v5 faster cursors; v6 voltage cursor fix; v7 tap first/last line to change V/div [p126-p129] |
| v0.26v9 | Oct 10 2025 | BASE calibration messages [p131] |
| v0.27j | Nov 1 2025 | scope.bmp/updater.bmp; black-screen report [p135] |
| v0.27s, u | Nov 2025 | trigger find at 50%, trigger-green indication [p136 #3381] |
| v1.00l6-l8 | Nov 30 - Dec 1 | first FPGA-aware DSO [p137-p138] |
| v1.00n, n3, n8, n9 | Dec 5-23 | ACQ depth buttons 3000-15000; Trace; n9 modified LINEAR plot [p139-p141] |
| v1.00o - o8 | Jan 2 - Mar 1 2026 | stock-FPGA AUTO support; EF2 6144 support; trigger address calc; o8 removes longer read in auto [p142-p149] |
| v1.00p0-p9 | Mar 17 - May 2026 | version scheme; dynamic trigger marker; p9 with FPGA 2014/3014 [p150-p152] |
| v1.00q2 | May 22 2026 | AL3 12,000 message fix; waveform cleared on trigger-mode change [p152 #3792] |

### 4.3 Features, bugs, requests
- REF waveforms up to 8, saved in "refforms" on SD, Y-move, colours; bad CRC makes slow start; cycle measure on refs wrong because sampling frequency not matched [p126-p127 #3154, #3163].
- Touch zones: left of pink line REF, middle CH1/CH2 position (within +-30 units of marker), right trigger; v0.26v7+ tap first/last line for V/div [p128 #3184, #3199; p129 #3205].
- Requests: peak detect, roll mode (unsupported), hold-off (not in any FPGA), FFT (disabled in 1.00 line, separate dev branch), protocol decoders (only if someone writes them), Bode (no) [p124; p136 #3377-#3378; p137 #3412; p153 #3806-#3812].
- Single shot: set level, Single, Start; Start green, orange WAIT, red when triggered [p137 #3420].
- "Failed to open file" at boot: missing scope.bmp, bad SD or space [p140 #3522; p151 #3766]. 3-second black screen when no scope.bmp [p136-p137 boojum].
- Atlan planned to drop original-firmware support (Nov 2025), later keeps return path via backups [p136 #3383; p149 #3705].

## 5. Measurements and limitations summary
- Single-channel 200 MS/s is a hardware limit (see 2.2) [p122 #3033].
- Display 723 points; memory 3000-15000 decimated [p139 #3467]. 100 MSa/s with 12000 samples should be 120 us, display showed 150 us (under test) [p147 #3657].
- Linear mode skips samples and can hide signals; DOT mode more stable [p146 #3631; p150 #3729].
- Accuracy: agrees with Rigol DHO814 for NE555 pulses [p122 Russell #3029]; AC RMS 214 V on 220 V mains vs 219 V stock [p123 King_E #3073].
- Dual-channel trigger (0x2001+) and PLL change reduce jitter and jumping [p150-p151].

## 6. External links (exact)
- https://github.com/Atlan4/Fnirsi1013D/tree/main/Guide%20to%20firmware
- https://github.com/Atlan4/Fnirsi1013D/tree/main/Verzia%201.xxx/FPGA%20for%201.xxx
- https://github.com/pecostm32/FNIRSI_1013D_Firmware and .../blob/main/configuration_file.txt
- https://github.com/pecostm32/Hantek_DSO2000 ; https://github.com/pecostm32/Anlogic_AL3-10_Analyzing
- https://github.com/pecostm32/FNIRSI-1013D-1014D-Hack/tree/main/Schematics/1013D (schematics) [p132 #3279]
- https://github.com/pecostm32/Lichee_Nano/tree/main/Hardware/Anlogic_JTAG_Programmer
- https://github.com/AnlogicInfo/anlogic-usbjtag
- https://github.com/mmicko/prjtang/blob/master/docs/architecture/bitstream_format.rst
- https://www.eevblog.com/forum/fpga/reverse-engineering-anlogic-al3_10-fpga/
- https://www.eevblog.com/forum/testgear/new-bench-scope-fnirsi-1014d-7-1gsas/msg4102147/#msg4102147
- https://ww1.microchip.com/downloads/en/DeviceDoc/MIC29302A.pdf
- https://wiki.sipeed.com/hardware/en/tang/Tang-primer/get_started/install_linux.html (TD on Linux)
- https://www.dreamsourcelab.com/shop/oscilloscope/dstouch-ds4t252/

## 7. Ideas and wishes not implemented
- Hold-off and further trigger work (ObsoleteDIY offered) [p153].
- AL3 firmware at 15,000 samples; EP4CE10 for more memory [p143, p152].
- Roll mode, FFT, decoders, Bode, PC control (CDC works, app is demo) [p135, p153].
- FPGA update without desoldering: only the wiring mod exists; EF2 SPI header idea dropped [p130 #3245; p153 #3820].
- Port EF2 improvements to AL3 and EP4 [p149 #3702].
- Individual time-shift of signals/refs [p153-p154 facekim].
- Hardware jitter fix via better clock (inference by TriNitron) [p141].

## 8. Open questions / contradictions
- Touch zone tolerance +-15 [p128 #3184] vs +-30 [p128 #3199].
- Post #3211 "v0.27v7" vs announced v0.26v7 (likely typo) [p129].
- EF2/AL3 circular-memory constant 6143 vs 6144; old +5393 [p148 #3697; p149 #3716].
- Max samples: AL3 "16k/15k" [p146 #3631], "AL3 12,000 with FPGA 2001" [p150 #3742], AL3 fixed count 6143 per ADC [p150 #3738], v1.00q2 fixed an AL3 12,000-limit message [p152 #3792].
- Revert to stock after mod: pcprogrammer says original firmware no longer available; Atlan says possible through backups [p148 #3690 vs p149 #3705].
- AL3 FPGA flash identity: 25VQ80 1 MB (Atlan) vs 25VQ16ASIG (mishail71) vs Winbond 25Q80DV (ded) vs ZB25VQ80 [p138-p142].
- FNIRSI download date 14.06.2024 vs 2025 assumption [p125 VVV121].
- Bootloader versions: 0.06 (Oct 2025) vs 0.7/0.8 later; no authoritative table in these pages.
- TD 4.6.8 SP1 vs 5.0.x preferred: conflicting [p146 #3640; p154-p155].
- EP4CE6 compatibility with p-series DSO firmware untested [p143, p150].
- Romeo53's EF2 shows 0x1432 on screen after flashing 2014.bit, later explained as stock FPGA still active [p152 #3775; p154 #3827].
- ADC part number is only given as MXT2088 (via pcprogrammer); Felipe's ADC marking is scraped [p136 #3393].
- Claims of "Chinese developer, unoptimized" PC protocol are Atlan opinion [p148 #3676].
