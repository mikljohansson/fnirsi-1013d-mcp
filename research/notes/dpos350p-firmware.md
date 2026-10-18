# FNIRSI DPOS350P firmware image analysis (DPOS350P-FNIRSI-2025619.iso)

File: `research/dpos350p/DPOS350P-FNIRSI-2025619.iso`, 948310 B (0xE7856). Update log dated 2025-06-19.
Method: static analysis only. Scripts in `research/tools/` (list at end), run with `python -I`. Nothing from the image was executed.
Legend: FACT = read from bytes. INFERENCE = my reading. Offsets are file offsets unless marked "addr".

## 0. Bottom line
- SoC: Allwinner **V3s** (sun8i, single Cortex-A7, ARMv7-A, hard-float VFP, DRAM base 0x40000000). Different from the 1013D (F1C100s, ARM926, soft float, DRAM 0x80000000). FACT: USB MSC INQUIRY string "V3S Disk", USB product string "V3S USB Devices", and the peripheral map matches the V3s (section 3).
- The image is NOT a boot0/SPL image. It is a flat concatenation: a 4 KiB name header, a boot-logo container with an "eGON.BMP" magic, then one "eGON.EXE" application (the whole oscilloscope firmware, ~610 KB). Same `eGON.EXE` convention the 1013D scope/startup images use (length at +0x10, payload from +0x20). DRAM init is not in this file, it must live in the device's resident bootloader (INFERENCE).
- Bare-metal, no RTOS/LVGL strings. Libraries: FatFs with exFAT + cp936 (GBK) tables, an Allwinner-style sunxi UDC USB gadget driver, a Nuvoton-style SD ("fmi") driver, Allwinner SDMMC/SPI drivers.
- Hard float is used heavily (2465 VFP/NEON instruction words in the code region, double constants incl. 2*pi at 6 sites), so FFT / FRA / generator math runs in software on an A7 with FPU.
- No serial decoder strings (no I2C/UART/SPI/CAN decoders), no math channel strings, no "Roll" label (rolling mode appears only in the update log).

## 1. Offset map (file offsets)
| Range | Size | What | Notes |
|---|---|---|---|
| 0x0000-0x0007 | 8 | ASCII "DPOS350P" | model tag. 0x0008-0x0FFF all zero. No section table, no CRC table, no size/offset fields. |
| 0x1000-0x101F | 0x20 | "eGON.BMP" pseudo header | see 2.1 |
| 0x1020-0x19A00 | ~0x188A0 | boot logo pixels, RGB565 LE, 529x95 | FNIRSI wordmark, rendered and confirmed visually |
| 0x19A00-0x50FFF | 0x37600 | zero fill | |
| 0x51000-0x5101F | 0x20 | "eGON.EXE" header | see 2.2 |
| 0x51020-0x5105F | 0x40 | ARM vector table (addr 0x40000000) | 8x `ldr pc,[pc,#0x18]`, slot 5 (reserved) is 0, literal table follows |
| 0x51060-0x6A000 | ~0x18FA0 | bitmap font data (INFERENCE) | 1 bpp, big-endian 16-bit rows, CJK-like glyphs, ~3200 glyphs at 32 B. Glyph geometry/alignment not pinned down. |
| 0x6A000-0xBF000 | 0x55000 (340 KB) | ARM code (A32) | ~87k words, 8393 `bl`, 90%+ of words have cond=AL |
| 0xBF000-0xD2000 | 0x13000 | initialised data, mostly zero | preset-like tables (e.g. repeated 0x64000000 at 0xBF000) |
| 0xD2000-0xDD000 | ~0xB000 | rodata / tables, low entropy (3-4 bits) | |
| 0xDD810, 0xDDAB8 | | doubles: 1/sqrt(2) and pi | |
| 0xDDA90-0xDDB47 | | FatFs OEM case table (cp437 ExCvt) | |
| 0xDDB48-0xDDBC0 | | reset code (addr 0x4008CB28), IRQ stub at 0xDDD14 (addr 0x4008CCF4) | `b .` x4 at 0xDDD04 for undefined/svc/pabt/dabt |
| 0xDD95F-0xE145F | ~0x3B00 | string table: driver debug text + UI text in 4 languages | section 4 |
| 0xE1460-0xE4EF0 | ~0x3A90 | high-entropy (7.2 bit) 2-byte-record table | INFERENCE: FatFs cp936 (GBK) Unicode<->OEM table |
| 0xE4F10 | 8 | "DPOS350P" | right after that table |
| 0xE5F10-0xE60C0 | 30 B stride | generator waveform name table | Sin, Square, Triangular, Sawtooth1, Sawtooth2, Step, Half, Full, Exponent, Ringing, Sqrt, Multi audio, Sinc, Capture |
| 0xE62D0-0xE631F | | panel timing struct (addr 0x400952B0) | section 6 |
| 0xE6320-0xE635F | | SCSI INQUIRY data "XTQKJ   V3S Disk        1.0 " and "XTQKJ   V3S RAM Disk    1.0 " | |
| 0xE6360-0xE63FF | | 5 x USB string descriptor "V3S USB Devices" (UTF-16LE) | |
| 0xE6400-0xE641F | 0x20 | USB config descriptor, MSC bulk-only | |
| 0xE6430-0xE6490 | | 4 USB device descriptors | section 5 |
| 0xE6800-0xE782F | | zero (eGON.EXE payload ends at 0xE6800 per its header) | |
| 0xE7830-0xE7855 | 38 | trailer `00 00 04 01` + 34 high-entropy bytes | unknown. Not MD5/SHA1/SHA256/SHA512/CRC32 of the prefixes I tried. INFERENCE: signature or vendor stamp. |

Entropy per 4 KiB: 0x2000-0x19000 about 1-2.6 (logo), 0x19000-0x50FFF zero, 0x51000-0xBF000 5.5-6.5 (font + code), 0xBF000-0xD1000 0.3-1.0, 0xE1000-0xE4FFF 7.1-7.3 (cp936 table).
Magic scans: no gzip/zlib/LZ4/zstd/xz/PNG/squashfs/DTB/ELF. The "lzma" (5D 00 00) and one "jpg" hit are random matches in font/code, none validate. No compressed blobs, no FPGA bitstream, no filesystem image, no second-stage loader in the file.

## 2. Headers
### 2.1 "eGON.BMP" at 0x1000 (FACT unless noted)
```
0x1000  ea000006        b 0x1020 (standard sunxi jump, lands in zeros, not executable)
0x1004  "eGON.BMP"      magic (boot0 would be "eGON.BT0")
0x100C  0x6B144BAD      "check_sum"
0x1010  0x00018A00      length (100864)
0x1014  "BMP\0"         (where sunxi puts the "SPL" signature)
0x1018  0x02111000      bytes 00 10 | 0x0211 = 529
0x101C  0x1B01005F      0x005F = 95 | 0x1B01
0x1020  0x02111000, 0x1B01005F again, rest zero
```
- INFERENCE: width 0x0211 = 529 and height 0x5F = 95. Confirmed independently: autocorrelation of the pixel data gives a 529 px row stride and the render shows the FNIRSI logo, 95 rows.
- Standard sunxi checksum (sum of 32-bit words over `length`, stamp 0x5F0A6C39 in the checksum field) gives 0x51705FD6, not 0x6B144BAD. Either another algorithm or not verified. This is not a BROM-bootable image.
- Possible link to the Atlan4 tree folders "updater 0.02c - add updater startup picture" and "0.26r2 BMP time stamp" (not diffed).

### 2.2 "eGON.EXE" at 0x51000
```
0x51000  ea000006     b 0x51020
0x51004  "eGON.EXE"
0x5100C  0x00000003   unknown field (not a checksum)
0x51010  0x00095800   length (610304), image ends at 0xE6800
0x51014  "EXEC"
0x51018  zeros
0x51020  payload start = load/run addr 0x40000000
```
- Link base 0x3FFFFFE0 including header, like the 1013D scope image linked at 0x7FFFFFE0 (repo-pecostm32.md: length read from bytes 16..19, copy from +32).
- Address mapping (FACT, verified by code-to-string pointer cross references): `addr = 0x40000000 + (file_off - 0x51020)`. Example: "Frequency response analyzer" at file 0xDED5C is addr 0x4008DD3C and is referenced from a literal at file 0x970BC.
- Reset code (addr 0x4008CB28, file 0xDDB48): `cpsid i`, sets IRQ/SYS/SVC stacks from literals 0x43068D28/0x42EE8D28/0x42FE8D28, clears SCTLR.V, copies the vector table (two ldm/stm of 8 words) to 0x0 or 0xFFFF0000, then `bl 0x4008CB94`, `b 0x4008CBBC`. No DRAM or PLL init here, DRAM is already up at entry.

## 3. SoC / CPU identification
- Instruction set: ARM A32 with ARMv7 instructions (`cpsid i`, `cps`, `movw/movt`, CP15 SCTLR access, `subs pc,lr,#4`, VFP). Not RISC-V, and not ARM9 (ARM926 has no movw/movt or VFPv3). Capstone disassembles it cleanly.
- DRAM at 0x40000000: stacks and buffers at 0x42E3xxxx-0x4306xxxx.
- Peripheral accesses found (movw/movt, literals, `mov #0x1c00000` + `add` pairs), all V3s map:
  - 0x01C0C000 TCON0, timing code at addr 0x4003E064 (file 0x8F084)
  - 0x01C0F000 SDMMC0
  - 0x01C19000 USB OTG (regs 0x40..0x98, 0x410), 0x01C19400 USB PHY control (8 uses)
  - 0x01C20000 CCU, 0x01C20800 PIO, 0x01C20C00 timer
  - 0x01C22800 LRADC, 0x01C28000 UART0, 0x01C28800 UART2
  - 0x01C68000 **SPI0 (6 uses, the heaviest bus user)**
  - 0x01CB4000, 0x01010000, 0x01C10000 touched once each (CSI / display engine / SDC1, not identified)
- SoC-naming strings: "V3S Disk", "V3S RAM Disk", "V3S USB Devices", "sunxi_udc_bsp_init", "sunxi_udc_enable", "usb_phy_open_clock_Device". No "F1C", "sun8i" or "T113" strings.
- DRAM init parameters and PLL setup are NOT in this image. The header words at 0x1018/0x101C are image dimensions, not DRAM parameters.
- Clock-like literals in code: 24,000,000 (x4), 25,000,000, 20,000,000, 50,000,000 (x7), 100,000,000 (x12), 150,000,000, 200,000,000, 350,000,000 (x2), 500,000,000, 600,000,000 (x4), 1,000,000,000 (x21).

## 4. Strings (file offsets)
Languages: Chinese (UTF-8), English, Portuguese, Russian. Language menu: 0xE1450 "中文", 0xE1458 "English", 0xDF7D4 "Русский язык", 0xE0F8C "Português". Every label exists in each language as separate blocks. Full dump: `dpos_strtab.py <iso> 0xDD900 0xE6800`.

### 4.1 UI
- Menu pages: System (系统设置), Function (功能设置), Measure (参数测量), Browser (文件查看), Cursor X (光标测量 X), Cursor Y (光标测量Y), Save wave (保存波形), Screenshot (保存截屏), Zoom time base (缩放时基), Configuration (配置设置). [0xE006C-0xE016C, 0xE0A7C-0xE0AD8]
- System: System calibration, Auto shutdown (15 min / 30 min / 45 min / 1 hour / Off, 0xE1318-0xE1330), Language settings, Disk formatting, Waveform light, Color temperature, Background grid, Factory settings, USB mode, Capture output (截波输出).
- Function: X-Y mode, FFT display, FFT length, Spectrum analyzer (频谱分析仪), **Frequency response analyzer** (频率响应分析仪), Waterfall display (瀑布图显示), 3D display, Cursor measurement, Zoom time base, Capture, Single mode / Loop mode, Start frequency, Stop frequency, Frequency count, Center freq, Freq width, Reference, Attenuation, Autoset, Fast move / Slow move (粗调/微调), text "( 20mS - 5nS )" at 0xE08EC.
- Channel: Channel enable, Probe attenuation, Coupling (DC/AC), Bandwidth limit ("Full 350M" 0xE0B3C, "150M" 0xE1360, 20M in self-test text), High resolution mode with **8 to 16 bit** labels at 0xE13F8-0xE1438.
- Trigger: mode (Auto/Normal/Single), edge (Rising/Falling), channel (T-CH1/T-CH2 at 0xE1440), HF suppression.
- Measurements: CH1/CH2 parameter measurement. ASCII names present: Vmax, Vmin, Vavg, Vrms (0xE1340-0xE1358), Frequency, Amplitude, Duty pulse (0xE0BC0). Other names are not ASCII (log mentions "Du" and "DT" names). Unit string "dBmV" at 0xE0FE0.
- Time base labels (0xE1368-0xE13F0): 500mS 200mS 100mS 50mS 20mS 10mS 500uS 200uS 100uS 50uS 20uS 10uS 500nS 200nS 100nS 50nS 20nS 10nS. Rate units KS/s, MS/s, **GS/s** at 0xE1300-0xE1310.
- Config slots: Save / Read / Power-on configuration, Configuration 1..5.
- File browsers: Waveform / Picture / Capture browser, Select all, Delete selected, page navigation, "Total ... items".
- Dialogs: Notice, Warning, Confirm, "Disk space is full !", "Screenshot saving ...", "Waveform saving ...", "Low battery shutdown !", "Auto shutdown !", "Please connect all channels to the signal generator !", "No signal detected !" (also per channel), "Please unplug all signal proble and USB first !", "Calibratting ...", "CH1 failed / CH2 failed / All failed".

### 4.2 Signal generator (FACT that one exists)
Waveform name table at 0xE5F10 (stride 0x1E): Sin, Square, Triangular, Sawtooth1, Sawtooth2, Step, Half, Full, Exponent, Ringing, Sqrt, Multi audio, Sinc, Capture. Adjustable: Frequency, Amplitude, Offset, Duty. "Capture output" (截波输出) replays a captured waveform. The 1013D open firmware has no generator.

### 4.3 Self-test / factory calibration (CN)
FPGA检测, ADC检测, 加密芯片检测 (**crypto/authentication chip check**), 通道复用检测 (channel multiplexing check), 通道量程检测, 通道DCAC检测, 通道平坦度检测, 150M带宽限制检测, 20M带宽限制检测, ADC时钟相位校准 (**ADC clock phase calibration**), 通道和信号发生器检测. Labels "ADC1".."ADC8" at 0xE0FF8-0xE1030 (8 labels). INFERENCE: several ADC lanes or phases, calibrated separately.

### 4.4 Driver / debug text (ASCII)
- USB: "usb init...", "usb enter high speed", "CONFIG_USB_GADGET_DUALSPEED: USBC_TS_MODE_HS", "sunxi_udc_bsp_init failed", HID_REQ_SET_REPORT/SET_IDLE, "set_line_coding / get_line_coding / set_control_line_state" (CDC ACM), "usb_uvc_get_cfg_descriptor" (UVC), "usb_mass_get_cfg_descriptor", "Valid CBW 1-4", "SCSI_Invalid_Cmd", "ERR:read10 address out of range!".
- SD/MMC: "mmc_config_clock ok...", "mmc%d tmier timeout", "fmiSD_Read_in(): uBufcnt cannot be 0" (Nuvoton FMI naming), "CARD_TYPE_SD_HIGH/LOW", "sd_nand_fatfs_check", "sd_nand_check_file.bin" (INFERENCE: storage is an SD NAND chip on SDC0).
- SPI: "SPI%d CLK[%dHZ][n=%d]...", "ERR:sys_spi_cpu_transfer timeout.", "ERR:spi transfer max len 0xffffff".
- Paths: /Screenshots, /Waveform, /Captures, /Screenshots/number_table.sys, /Waveform/number_table.sys, /Captures/num_table.sys, /Captures/data.cap. FS tags "NO NAME FAT32", "NO NAME FAT", "EXFAT", "MSDOS5.0" (FatFs mkfs).
- No scope/FPGA-level printf text and no version string inside the exe. The version is carried only by the file name (2025619).

## 5. USB
- MSC config descriptor at 0xE6400: len 0x20, 1 interface, self-powered (0xC0), 100 mA; interface class 0x08 subclass 0x06 protocol 0x50 (SCSI bulk-only); EP 0x81 IN and EP 0x01 OUT, bulk, 512 B.
- Device descriptors (bcdUSB 2.00, EP0 64 B):
  - 0xE6430: class 0x02 (CDC) VID 0x8888 PID 0x2222
  - 0xE6446: class 0x03 (HID) VID 0x8888 PID 0x1111
  - 0xE645C: class 0xEF/0x02/0x01 (IAD, UVC webcam) VID 0x0416 PID 0x9393
  - 0xE6472: class 0x00 (mass storage) VID 0x0483 PID 0x5720
- SCSI vendor "XTQKJ", products "V3S Disk" and "V3S RAM Disk". The update log's "USB tethering" step suggests the device exposes a RAM disk for the firmware copy (INFERENCE).
- INFERENCE: four selectable USB modes (mass storage, CDC serial, HID, UVC video). The 1013D firmware has MSC only.

## 6. Hardware hints
- **Display**: panel struct at file 0xE62D0 (addr 0x400952B0) is read by the TCON0 setup, which writes reg 0x48 as ((struct[+4]-1)<<16) | (struct[+0x14]-1). Values: +0x04 = 1022, +0x08 = 160, +0x0C = 164, +0x14 = 600, +0x18 = 12, +0x1C = 23. INFERENCE: **1024x600** panel (the 1022 is likely a deliberate -2). The 1013D is 800x480.
- **ADC / sample rate**: "GS/s" label present, and 1,000,000,000 occurs 21 times in code. INFERENCE: nominal **1 GSa/s**, probably by interleaving (see "通道复用"). A halving ladder of literals 950M, 475M, 237.5M, 118.75M, 95M, 47.5M, 23.75M, 11.875M, 9.5M, 4.75M, 2.375M is present and unexplained, next to 500M/250M/125M/100M/50M/25M/10M/5M. Literals equal to 10^10, 10^11, 10^12 (low 32 bits: 0x540BE400, 0xA43B7400, 0xD4A51000) suggest picosecond arithmetic with 64-bit math.
- **ADC resolution**: probably 8-bit; "High resolution mode" offers 9..16 bit (INFERENCE: averaging/decimation).
- **Bandwidth**: Full 350 MHz, 150 MHz, 20 MHz (FACT, strings).
- **Timebase**: 500 ms/div to 10 ns/div in the list, with an extra "( 20mS - 5nS )" range text. The 1013D open firmware covers 200 ms to 5 ns.
- **Memory depth**: NOT determined. No "pts/Mpts" strings. The 1013D has 3000 samples per channel.
- **FPGA**: only the string "FPGA检测". No part number, no bitstream in the image. SPI0 is the only heavily used bus, so FPGA control/data likely rides SPI0 (INFERENCE; the 1013D used a GPIO-driven parallel bus). SD NAND on SDC0. A separate "加密芯片" (crypto chip) is checked at self-test, interface unknown.
- Keys via LRADC (0x01C22800). UART0 (0x01C28000) is configured somewhere, likely a debug console (INFERENCE). Touch is not evidenced.
- Battery: "Low battery shutdown", and a log fix for battery power jumping.

## 7. Update log (DPOS350P Update Step & Log.txt)
Steps: extract, connect via "USB tethering" (PC ejects the drive), copy the extracted firmware into the drive folder, disable tethering and disconnect, power cycle, wait for the upgrade.
20250619: Du/DT measurement name fix. Rolling mode freeze fix. Phase difference between 2 channels fix. FRA minimum frequency now 10 Hz. FRA shows sampling progress percent. Battery power jump fix. Abnormal shutdown fix.

## 8. Feature inventory vs the 1013D open-source firmware (per notes/repo-pecostm32.md)
1013D open firmware: F1C100s, 200 MSa/s, 3000 samples/ch, 12 measurements per channel (Vmax, Vmin, Vavg, Vrms, Vpp, Vp, Freq, Cycle, Time+, Time-, Duty+, Duty-), XY mode, cursors, FFT not implemented, no generator, no roll mode, MSC-only USB, no UART.

| DPOS350P feature (evidence) | In 1013D FW? | Portability to the 1013D (F1C100s, no FPU) |
|---|---|---|
| FFT display, FFT length, spectrum analyzer in dBmV (0xE0B24, 0xE0F2C, 0xE025C, 0xE0FE0) | No (flag only) | Feasible in fixed point (radix-2, window, 3000 samples). DPOS code uses double + VFP, so do not port directly. |
| Frequency response analyzer (start/stop/center/width, 10 Hz min, progress %) | No | Needs a signal source. The 1013D has no generator output, so the sweep half is not portable. The analysis half (gain vs frequency from captures) is. |
| Built-in signal generator, 14 waveforms, Capture replay | No | Hardware dependent, not present on the 1013D. |
| Waterfall and 3D display | No | Display-only, portable in principle, low value, heavy on CPU and DRAM. |
| High resolution mode 8-16 bit | No | Likely portable (averaging of oversampled data), bounded by 8-bit ADC and buffer length. |
| Rolling mode, deeper acquisition | No (removed 2021-11-22) | Depends on FPGA RAM and logic. Probably not without FPGA changes. |
| 4 USB modes (MSC, CDC, HID, UVC) | MSC only | USB class code is software, so CDC/HID are portable in principle. UVC screen streaming is large work. |
| Config slots 1..5, power-on config, auto-shutdown, color temperature, brightness, 4 languages | Partial | Portable. |
| Waveform/screenshot/capture browsers (3 directories), exFAT via FatFs | Partial (FAT32, thumbnails) | Portable. |
| Zoom time base, Capture output | No | Portable (software). |
| Self-test and per-ADC phase calibration (ADC1..ADC8, flatness, 20M/150M tests) | Simpler baseline calibration | Hardware specific, not portable. |
| Serial protocol decoders, math channels | Neither | No strings in the DPOS350P image either, nothing to reuse. |

Code size context: ~340 KB of code and ~100 KB of font in a 610 KB app, no GUI toolkit, UI is hand-drawn as on the 1013D. FFT and measurement algorithms are locatable in the code if reversing is needed (2*pi literals at 0x92F74, 0x9EC28, 0x9F444, 0x9F7D8, 0x9FC88, 0xB21AC). Best lever for the 1013D project is a fresh fixed-point FFT and Bode-analysis implementation, not a port.

## 9. Open questions
1. Meaning of the word 3 at 0x5100C, and the algorithm behind the "check_sum" 0x6B144BAD at 0x100C (standard sunxi algorithm fails).
2. The 38-byte trailer at 0xE7830: does the loader verify it (signature)? Patched images may be rejected.
3. Where the FPGA configuration comes from (not in this image) and whether the image relies on a resident bootloader on SD NAND or SPI flash.
4. Memory depth, ADC part, FPGA model: need hardware or deeper reversing of the six SPI0 command sites.
5. Glyph format of the bitmap font at 0x51060-0x6A000.

## 10. Tools (research/tools/)
- `dpos_scan.py`: nonzero regions, per-4K entropy, eGON header decode, checksum test, magic scan, zlib probe.
- `dpos_hdr.py`: checksum variants for both headers.
- `dpos_disasm.py`: capstone ARM disassembly (`iso file_off length base_addr`). Capstone is installed into `tools/vendor/` (pip --target) and added to sys.path in the script, so `python -I` works.
- `dpos_consts.py`, `dpos_mmio.py`: movw/movt and literal pool constants, peripheral base grouping.
- `dpos_density.py`: per-4K ARM condition-code density (code vs data map).
- `dpos_strings.py`, `dpos_strtab.py`: ASCII/UTF-16LE strings and NUL-separated UTF-8 string table dump.
- `dpos_img.py`: RGB565 raw to PNG with stride autocorrelation.
- `dpos_trailer.py`: trailer hash match test.
