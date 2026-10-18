# FNIRSI-1013D/1014D-Hack: software reverse-engineering digest

Source: `D:\src\FNIRSI-1013D-1014D-Hack` (pecostm32). Paths below are relative to that root unless absolute.
Scope of this note: `Software reverse engineering/`, `Test code/`, `Linux/`, `Binaries/`, `Original firmware backup/`, `sunxi_stuff/`.
Not covered here: top-level `FPGA explained/`, `Schematics/`, `IC's/`, `Manuals/`, `Pictures/` (another agent).
Confidence tags: [CODE] stated by source or comments, [MEASURED] I verified against the binary images, [INFER] my inference, [NOT FOUND] absent from the repo.

## 0. Orientation

- Repo status per `readme.txt` (dated log, 2021-04 to 2021-11-02): hacking stopped; open firmware moved to https://github.com/pecostm32/FNIRSI_1013D_Firmware. `Test code/fnirsi_1013d_scope/` is an in-repo snapshot (Nov 2021) and the best reference for a new firmware.
- SoC: Allwinner F1C100s (ARM926EJ-S, ARMv5TEJ, 32 MB DDR in package, BROM with FEL). FPGA: Cyclone IV, reached over an 8-bit parallel bus bit-banged on PORT E. ADC: AD9288 x2 (`C analysis/check hardware/check_ad9288.c`).
- Documents that read like design notes (best first):
  1. `Software reverse engineering/C analysis/FPGA analysis/FPGA explained/FPGA explained.txt`, plus `Startup and signal data read sequence.txt`, `parameter_analysis.txt`, and 22 per-timebase `*-div_signal_data_read_sequence.txt` command traces
  2. `Software reverse engineering/C analysis/FPGA analysis/FPGA analysis.txt` (earlier version)
  3. `Software reverse engineering/C analysis/global variables` (RAM map)
  4. `Software reverse engineering/C analysis/startup and stack settings`
  5. `Software reverse engineering/C analysis/Display analysis/display_analysis` and `data_structures.c`
  6. `Software reverse engineering/C analysis/USB/descriptors.txt`, `USB/usb analysis`
  7. `Software reverse engineering/C analysis/I2C analysis/I2C_Code_Analysis.txt` (touch GT911)
  8. `C analysis/Clock analysis/clock analysis`, `C analysis/SD and FLASH analysis/SD card analisys`
  9. `Software reverse engineering/readme.txt` (Ghidra archive change log)
  10. `Binaries/Separated parts/readme.txt` (flash block layout), `Binaries/Hacked files/readme.txt`, `Binaries/Hacked files/tp_config_change_hack.txt`
  11. `Test code/Scope_emulator/readme.txt` (dated emulator log with FPGA discoveries), `Test code/f1c100s_ch340/readme.txt`
  12. `Test code/fnirsi_1013d_original_scope/todo.txt` (55 KB scratch/todo mixed with debug snippets; still has useful behaviour notes: touch-object priority rules, trigger-mode time base limits, SD quirks)

## 1. Stock firmware structure

### 1.1 Boot chain
| Stage | Where in flash | Loaded to | Entry | Notes |
|---|---|---|---|---|
| BROM | mask ROM | n/a | n/a | Looks for an `eGON.BT0` header on SPI-NOR or on SD at byte 8192. FEL USB mode if no valid boot media. |
| SPL, "second program loader" (`eGON.BT0`) | 0x000000, size 0x3400 | SRAM 0x00000000 (32 KB) | 0x0 | `C analysis/SPL analysis/SPL.c` `main()` about lines 308-380. Order: `sys_dram_init`, `sys_clock_init`, `sys_spi_flash_init`, `UART0_Init`, read bitmap, `FPGA_Init`, `sys_init_display(800,480,0x81b00000)`, `display_bitmap`, `Set_Default_Brightness`, then load program. |
| Bitmap (`eGON.BMP`) | 0x013000, size 0xE600 | payload (32 B header plus 8 extra stripped) to DRAM 0x81000000, blitted into framebuffer 0x81B00000 | n/a | RGB565 logo, 298x98 px (`Test code/bitmap_converter/convert_scope_bitmap.c`). Header: length u32 LE at +0x10, x px u16 at +0x1A, y px u16 at +0x1C. |
| Main program (`eGON.EXE`), "2nd executable" | 0x027000, size 0x193A00 (new W25Q32) or 0x193200 (old W25Q16) | payload (32 B header stripped) to DRAM 0x80000000 | 0x80000000 (ARM). SPL calls it after `delay(100)`. | `Binaries/Separated parts/2nd_executable_part.bin` is 1,653,215 B including header. |
| Update program, "1st executable" (`eGON.EXE`) | 0x006000, size 0x7A00 (new) or 0x7400 (old) | DRAM 0x80000000 | 0x80000000 | SPL only runs it when the 0x27000 header does not match. Firmware-update program (1.4). Hack: byte at file offset 1560 changed 0x27 to 0x06 turns `mov r0,#0x27000` into `mov r0,#0x6000` so SPL always runs it (`Binaries/Hacked files/readme.txt`). |
| Block header (32 B) | all blocks | | | `+0x00` ARM branch `0xEA000006`; `+0x04` ASCII `eGON.BT0` / `eGON.BMP` / `eGON.EXE`; `+0x0C` checksum (nonzero for BT0 and BMP, zero for EXE in the sample images [MEASURED]); `+0x10` length u32 LE including header; `+0x14` `EXEC` (EXE) or `SPL\x02` (BT0). |

[MEASURED] in `Binaries/Original files/W25Q32_scope.bin`: 0x6000 len 0x7A00, 0x13000 len 0xE600, 0x27000 len 0x193A00.

### 1.2 Runtime memory map (DRAM 0x80000000-0x81FFFFFF, 32 MB)
From `C analysis/startup and stack settings`, `global variables`, `Display analysis/display_analysis`:
- Program image occupies 0x80000000-0x801939DE. Initialised data is unpacked at startup by an RLE-like routine `do_some_initial_byte_handling` (`Startup/do_some_initial_byte_handling.c`): 0xA18 bytes first, then 0xA704 (42,756) bytes from 0x80192E9C to 0x8019D5A0 (includes touch-panel config and USB descriptor tables). Reimplemented in `Test code/initial_data_unpacker/initial_data_unpack.c`; output `initial_scope_data.bin` 42,756 B.
- Stacks, all inside the top ~400 KB: UND 0x81FB5000, ABT 0x81FC1800, IRQ 0x81FDA800, FIQ 0x81FE7000, SYS 0x82000000, SVC 0x81FF3800. IRQ entry saves a 0x48-byte frame below 0x81F83000 and calls `FUN_8001C530` (dispatcher). Only IRQ is used; undefined/SWI/abort/FIQ vectors branch to themselves.
- Globals: 0x8019D5A0 channel/timebase/trigger settings (offsets in `global variables`), 0x801FA24C measurement-settings block (+0x000 enabled list, +0xE4.. results, +0x100..+0x282 per-item enables, +0x292 time cursors, +0x29A volt cursors), 0x80361378 UI state (+2 screen brightness, +3 grid brightness, +4 always-50% trigger, +7 x-y mode), 0x802F18B0 calibration block, 0x8035344E raw settings buffer from flash (500 B), 0x802F19CE list-file buffer (400,000 B), 0x8035A97E system-file buffer (2,000 B). `sys ok` flag at 0x8019D5E4 must equal 0x1432.
- Trace buffers (stock): CH1 0x8019D5EA (12,000 B = 6,000 shorts, also long-timebase average), CH2 0x801A04CA, CH2 second read 0x801A1C3A, processed CH1 0x801A916A, processed CH2 0x801AA8DA, long-timebase results 0x801AC04A and 0x801AD7BA, DRAM target of the 0x21 reader 0x8019ED5A (one 16-bit slot per sample).
- Display (RGB565, 800x480, 768,000 B per buffer): scan-out 0x803849A0, frame buffer 0x804FB9B4, frame buffer 2 0x804401B4, extra buffer 0x805B7228, display info struct 0x805B71B8, active drawing pointer 0x8019CF60, display/font state 0x80857D4C. Library draws into the pointed buffer; `copy_frame_to_screen` (`FUN_8001CD34`) copies 0x804FB9B4 to 0x803849A0.

### 1.3 SPI flash layout (W25Q32 = 4 MiB; W25Q16 variants = 2 MiB) [CODE + MEASURED]
| Offset | Size | Content |
|---|---|---|
| 0x000000 | 0x3400 | SPL (`eGON.BT0`, BROM checksum in header) |
| 0x006000 | 0x7A00 (old 0x7400) | firmware-update program |
| 0x013000 | 0xE600 | startup bitmap |
| 0x027000 | 0x193A00 (old 0x193200) | main scope app. Its tail holds the packed init-data blob (touch config near 0x1B9E94, USB descriptors near 0x1BA6FB) and fonts/bitmaps. |
| 0x1BAA00-0x1F3000 | | zero filled (not 0xFF) in the sample image [MEASURED] |
| 0x1F3000-0x1FD000 | | erased 0xFF |
| 0x1FD000 | 0x1F4 (500 B) | settings and calibration block (section 3.3). `load_config_data` at `Startup/restore_config_data.c:5`. |
| 0x1FE000 | ~0x100 | First u16 is the hardware-check selector: 0x9086 skips the FPGA/hardware check (sample has 0x9086), 0x7293 is an alternate value, anything else runs it (`FUN_8002330C`, `Startup/hardware_checking.c`). Rest looks like a dumped struct with RAM pointers (0x800273D0, 0x80024F98, 0x8035ED5C) [MEASURED, meaning INFER]. |
| 0x1FF000 | | erased |
- Flash access: SPI0 on PORT C (5.2), read cmd 0x03 with 3-byte address, write enable 0x06, busy poll via status (`SD and FLASH analysis/spi.c`).
- Old and new images share the structure with different block sizes.
- 1014D flash (2 MiB, `Test code/1014D_Emulator/FNIRSI_1014D_full_flash_backup.bin`) [MEASURED]: BT0@0 len 0x3400, EXE@0x6000 len 0xBA00, BMP@0x13000 len 0xE600, EXE@0x27000 len 0xBC400, settings at 0x1FD000 (starts `01 00 05 00`), 0x1FE000 holds 0x9086.
- Fonts: 8 fonts (`Test code/Scope_fonts/font_{0,2,3,4,5,6,7,8}.c`) extracted by `Test code/FNIRSI_Font_Extract/` reading the flash from 0x27000. RAM addresses: 0x8018A380, 0x8018A3A4, 0x8018AC58 (channel menus), 0x8018B738 (error messages), 0x8018C1C0, 0x8018D558, 0x8018F508, 0x801913B0. Two families: fixed width (courier-like) and variable width (Arial-like). The stock UI mostly blits pre-rendered bitmaps instead of text (readme 08-JULY-2021).

### 1.4 Firmware update mechanism (`C sub parts 1st executable/update_firmware.c`, `SD and FLASH analysis/check_for_firmware_upgrade.c`)
- Looks on the FAT SD card for `UTX-1013.bin`, `FSI-1013.bin`, `DAN-1013.bin`, `YPK-1013.bin` (vendor variants; first lines of `update_firmware.c`). `Original firmware backup/readme.txt` says rename the backup to `FNI-1013.bin`, but the code only knows those four names [discrepancy; code wins].
- Reads the whole file to RAM, programs flash from 0x13000 in 30 blocks of 0xFFFF bytes (progress bar), then re-reads flash and verifies **9 check dwords in the last 36 bytes of the file**: dword0 = sum of all bytes; dwords 1-8 = for bit 7 down to bit 0, the sum of byte indexes where that bit is set (big-endian in the file). Computed over `file_len - 0x48` bytes. On success the file is deleted. On failure it overwrites 1000 bytes at 0x27000 (so the SPL falls back to the update program next boot), then forces a watchdog reset.
- Packer (`Test code/flashfilepacker/flashfilepacker.c`): file offset 0 equals flash 0x13000. Output = existing out-file bytes up to `-l`, then input data at `-l`, then 36 zero bytes, then 36 check bytes (total `lastbyte + 72`). Update file for main app: `flashfilepacker -o FSI-1013.bin -i app.bin -l 0x14000` (bitmap already at start of the output file).
- Same packer builds SD boot images (2.4).

### 1.5 Hardware init order in stock `main` (`C analysis/Startup/main.c`)
`sys_clock_init` (CPU PLL 600 MHz), `setup_mmu` (caches only), `sys_init_uart0(115200)`, `setup_timer_int`, `sys_spi0_init`, `fpga_init`, `turn_off_brightness`, `sys_init_display`, `setup_display_lib`, `tp_i2c_setup`, `check_sd_card` (shows SD ERROR and hangs if no usable card, so **the stock scope requires a FAT SD card**), `check_hardware` (only when flash 0x1FE000 is not 0x9086), USB setup, `setup_power_mon_int`, `load_config_data` plus `check_sys_ok_load_config_on_fail` (sys-ok word must be 0x1432 else defaults are written back), FPGA channel/trigger setup, `FUN_8000A024` (system file check, "System file damaged!!!"), `setup_main_screen`, then loop `{monitor_battery; process_trace_data; display_cursors; touch_handler}`.

### 1.6 Functions and structures identified (stock addresses)
| Area | Key items | Files |
|---|---|---|
| Display library | `sys_init_display` 0x8001D5D4 (PD0-21 as LCD, DEBE/TCON); `init_debe(x,y,addr)` 0x800163CC; `set_display_color` 0x80019704; `display_fill_rect` 0x80018F6C (start/end coordinates, not position/size); `display_text` 0x80018BF8; font select 0x80019730 and `FUN_800197C8` (render modes 0 fg on bg, 2 fg only, 1/3 xor). About 67 drawing functions use pointer 0x8019CF60. Structs `DISPLAYSYSTEM`, `DISPLAYDATA`, `FONTDATA`, `FONTMETRICS` in `Display analysis/data_structures.c`. A font is a linked list {first_char, last_char, metrics*, next}; fixed-width fonts use a separate struct with a translation table. | `C analysis/Display analysis/*` (80+ FUN_*.c) |
| Menus / UI | `setup_main_screen`, `setup_channel1_menu`, `handle_main_menu_touch`, `top_menu_handler`, `touch_handler` (118 KB of C), `measures_menu`, `save_picture(_file)`, `setup_view_screen`, `display_thumbnails`, `handle_view_mode_touch`. | `C analysis/Main loop/**` |
| FPGA bus | `FUN_800169F8` write command; `FUN_800168FC` write data; `FUN_80016850` read data; `FUN_800167A0` bus to input; `FUN_800168A0` init; `FUN_8001B058` / `FUN_8001B260` (cmd 0x24 / 0x26: read N bytes and sum); `FUN_8001B0F4` / `FUN_8001B2FC` (cmd 0x21 / 0x23: read 1500 or 750 bytes into 16-bit slots); `FUN_8001AF9C` / `FUN_8001B1A4` (command number taken from the parameter IC); `FUN_800011A4` (100-byte ADC check). | `C analysis/FPGA analysis/*` |
| Parameter IC | `FUN_800248F8` (write then read with delays), `FUN_80024EE0` (write), `FUN_80024E18` (startup read, fetches the XOR-crypt byte). Frame via FPGA commands 0x68-0x6E. | `FPGA analysis/FUN_800248F8.c`, `FUN_80024EE0.c`, `parameter_storage.c` |
| Trace capture | `process_trace_data` (28 KB; long timebase 100 ms to 50 S/div roll mode with 0x24/0x26 sums, short timebase with 0x20-0x23 bulk reads), `first_function_trace`, `second_function_trace`, `process_short_timebase_data_1/2`, `scope_pre_process_{50,100,250}ns_data`, `scope_process_ch1_25ns_data`, `scope_up_sample_x_{2,5,10,25}`, `up_sample_2.5`, `up_sample_4`, `scope_interleave_samples`, `scope_process_trigger`, `channel_1_data_processing`. | `Main loop/**`, `Main loop/trace data processing/*` |
| Measurements | `scope_display_measurements`, `scope_display_cursor_measurements` (32 KB), `measures_menu` (66 KB). 12 items per channel: Vmax, Vmin, Vavg, Vrms, Vpp, Vp, Freq, Cycle, Tim+, Tim-, Duty+, Duty-. | `Main loop/trace data processing`, `Main loop/measures_menu.c` |
| FFT | Only per-channel enable flags and picture/thumbnail references. **No FFT math function was analysed** [NOT FOUND]. | |
| Auto set / calibration | `perform_auto_set.c` (15 KB), `perform_50_percent_trigger_setup`, `double_channel1_samples`; `do_baseline_calibration.c` (24 KB: finds trace offset limits at 5 V/div, per-range DC offsets, verification loop); `fpga_setup_for_calib`, `fpga_start_calib_conversion`, `fpga_check_conversion_ready`, `fpga_read_channel1_adc1_samples`. | `C analysis/Auto setup`, `C analysis/Calibration` |
| USB (stock) | Device-type table in the init data: HID (VID 0x8888 PID 0x1111, and 0x0416:0x9393), others, and mass storage (type 4, the only one used). Leftover CDC strings ("CDC ACM config", "CDC Abstract Control Model (ACM)", "CDC ACM Data") and ST VCP IDs 0x0483:0x5740 exist but are unused. Active device: VID 0x0483 PID 0x5720, strings "H2750  Usb Devices" and "HeroJe Scanners Drives - H2750"; config 1 interface class 8 subclass 6 protocol 0x50 (BOT), EP 0x81 bulk IN 512 and 0x01 bulk OUT 512, 100 mA (0x32), EP0 64. | `USB/descriptors.txt`, `enter_usb_connect.c`, `setup_usb_int.c`, `usb_interrupt_handler.c`, `usb_handle_ep0.c`, `usb_mass_setup_handle.c`, `usb_handle_mass_storage_write.c`, `sys_init_usb.c` |
| USB (model) | MUSB at 0x01C13000, IRQ 26 (`setup_interrupt(1,0x1a,...,prio 2)`). PIO only. After enabling it waits 3 s (`delay_2(3000)`); disabling too early crashes. Entered from a menu (`enter_usb_connect`) and when the update file fails its check. SD card exposed raw via SCSI, so the scope is idle on a USB screen. | `USB/*` |
| SD / FatFs | U-Boot `sunxi_mmc` derived driver (`sunxi_mmc.c`, `mmc.h`, `mmc_base.h`). SDC0 at 0x01C0F000, pins PF0-5 function 2, clock gate bit (interface+8) in BUS_CLK_GATING_REG0, 400 kHz init then 48 MHz 4-bit (periph PLL 576 MHz). FatFs identified (`f_mount`, `f_open`, `f_read`, `f_write`, `f_stat`, `f_close`, `mount_volume`, `disk_read/write/ioctl`). Buffers must be 4-byte aligned. Stock uses DMA for large blocks; the open code uses CPU FIFO. | `SD and FLASH analysis/*` (`rle_reversed.txt` is a 383 KB data dump) |
| Settings save | `save_config_data` (6.6 KB), `restore_config_data` (15 KB), `reset_config_data`, `check_config_ok_refresh_it_on_fail`. Save is triggered by the power-off interrupt (PE11 external interrupt, INTC IRQ 0x27, prio 3: `power_off_interrupt_handler` calls `signal_sys_ok` then loops forever). | `Startup/*` |
| Battery | KEYADC (0x01C23400 cfg, 0x01C2340C data, 6-bit); charger pin PE12 (low = charging). | `Test code/fnirsi_1013d_scope/power_and_battery.c` |
| Touch | GT911/GT9157 over bit-banged I2C on PORT A: PA0 RESET, PA1 INT, PA2 SDA, PA3 SCL. Device address 0x14 (0x28/0x29) selected by driving INT high at reset. The coordinate register address is **returned by the FPGA** (cmd 0x41, always 0x8150) and the firmware loops forever if it differs. Status reg 0x814E. About 200 kHz in the open code. Stock config (`Startup/tp_config.txt`) is 1024x600 raw range scaled to 800x480. | `I2C analysis/*`, `Test code/fnirsi_1013d_scope/touchpanel.c` |
| UART | UART0 is on PE0/PE1, which are FPGA data bus bits, so `uart_printf` output goes nowhere useful. | `UART analysis/FUN_800376F8.c` |
| Misc | `Timer_ticks` (1 ms from TMR0), `delay_2(ms)`, watchdog reset, hardware checks (`check_ad9288.c`, `check hardware/*`). | |
Stock firmware has about 754 functions (`display_analysis`).

## 2. Binaries, SD images, FEL flows

### 2.1 `Binaries/`
| File | Size (B) | What |
|---|---|---|
| `Original files/W25Q32_scope.bin` | 4,194,304 | Author's full dump, new-style scope. Settings at 0x1FD000 are his unit's calibration. |
| `Original files/W25Q32.bin` | 4,194,304 | Second W25Q32 dump (same SPL checksum bytes) |
| `Original files/W25Q16cn.bin`, `W25Q16en.bin` | 2,097,152 each | Old 16 Mbit scopes |
| `Original files/FPGA Flash/New version/W25Q80_FPGA.bin`, `Old version/ST25P16VP.bin` | | FPGA configuration flash images (FPGA agent's area) |
| `Separated parts/SPL (second program loader) part.bin` | 13,279 | SPL without BROM header |
| `Separated parts/1st_executable_part.bin` | 31,199 | update program |
| `Separated parts/bitmap part.bin` | 58,839 | RGB565 logo |
| `Separated parts/2nd_executable_part.bin` | 1,653,215 | main app plus 32 B header |
| `Hacked files/W25Q32_scope_tp_conf_changed.bin` | 4 MiB | Touch-config patch applied (reversed RX/TX sensor lists for GT9157 panels). Author's 1-May-2021 fix. |
| `Hacked files/W25Q32_scope_tp_conf_not_written.bin` | 4 MiB | variant that does not write the TP config at all |
| `Hacked files/W25Q32_scope_1st_exec.bin` | 4 MiB | forces the update program to run |
| `Hacked files/W25Q32_scope_removed_FUN_8001794C.bin` | 4 MiB | `tp_i2c_setup` removed (proved it is the touch init) |
| `Hacked files/config_data.c` | 3,160 | the 500-byte block from 0x1FD000 of the author's scope |
| `Hacked files/sunxi_stuff/` and `sunxi_stuff/` | `fel-sdboot.sunxi` 8,192; `sunxi-fel` 77,304 | FEL boot SD image and sunxi-fel with SPI-flash support (Icenowy `f1c100s-spiflash` branch) |
| `New software/fnirsi_1013d.bin` | 256,072 | SD-card boot image (2.4) |
| `New software/fnirsi_1013d_scope.bin` | 93,696 | early open scope, FEL image |
| `New software/fnirsi_1013d_startup_with_fel.bin` | 9,216 | SD stub that returns to FEL |
| `New software/sunxi-fel` | 77,304 | Linux ELF |
| `Original firmware backup/fnirsi_1013d_fwb.bin` | 180,296 | SD package that backs up the flash (2.5) |
- Touch-panel hack (`Binaries/Hacked files/tp_config_change_hack.txt`), edits in the RLE-packed init data: byte at 0x1B9E94 0x18 to 0x1B; from 0x1BA651 reverse the RX sensor list (0x14..0x02 become 0x02..0x14); byte at 0x1BA65E 0x10 to 0x11; from 0x1BA65F insert 18 bytes `28 10 02 04 06 08 0A 0C 1D 1E 1F 20 21 22 24 26 28 24` (reversed TX list plus a "copy 4x 0xFF" opcode); 0x1BA671 must read `49 01`; remove three zero bytes at 0x1BA912 (they sit just after the config and are read to decide on the hardware check). The comparison I ran between `W25Q32_scope.bin` and `_tp_conf_changed.bin` shows differences clustered around file offset 1,812,050 (0x1BA652), consistent with this note [MEASURED].

### 2.2 FEL workflow (exact commands) [CODE]
One-time SD prep: `sudo dd if=fel-sdboot.sunxi of=/dev/sdX bs=1024 seek=8`. Host sees USB `1f3a:efe8` ("Onda V972 tablet in flashing mode").
- Check: `sudo ./sunxi-fel version`; `sudo ./sunxi-fel spiflash-info`.
- Flash read: `sudo ./sunxi-fel spiflash-read 0 4194304 test.bin` (2097152 for 2 MiB parts).
- Flash write: `sudo ./sunxi-fel -p spiflash-write 0 W25Q32_scope_tp_conf_not_written.bin`.
- Run a program with DRAM initialised: write `fnirsi_1013d_startup_with_fel.bin` to the SD with `dd ... seek=8` (clock, DRAM, SPI, display, FPGA init, logo, then `mov pc,0xFFFF0020` back into BROM FEL), then either
  - `sudo ./sunxi-fel -p spl fnirsi_1013d_startup_with_fel.bin` (alternative, no SD), and
  - `sudo ./sunxi-fel -p write 0x7FFFFFE0 fnirsi_1013d_scope.bin exe 0x80000000`.
  The image is linked at 0x7FFFFFE0 so its 32-byte eGON header sits just below 0x80000000 and code starts at 0x80000000.
- Common linker script: `MEMORY { ram : org = 0x7FFFFFE0, len = 32M }`; startup code clears BSS with `memset(BSS_START..BSS_END)`.
- Build: `arm-none-eabi-gcc` ..., `arm-none-eabi-objcopy -O binary x.elf x.bin`, then `./mksunxi x.bin` (BROM checksum and length), then optionally `./flashfilepacker` (`Test code/*/Makefile` post-build lines).

### 2.3 Toolchain conventions for `Test code/`
- Firmware projects: C and ARM assembly, arm-none-eabi-gcc, NetBeans (Makefile plus `nbproject`). Host tools: C for Linux with Xlib/pthreads.
- `mksunxi` (17 KB ELF from sunxi-tools) and `flashfilepacker` are copied into every project directory. Hand-written word-optimised `memcpy.s`, `memmove.s`, `memset.s` replaced slow loops (UI "sliding speed" matched the original only after this, `fnirsi_1013d_original_scope/todo.txt`).
- `compile.sh`, `burn.sh`, `debug.sh` in many projects are STM32 openocd leftovers; ignore.

### 2.4 SD-card boot (no case opening) [CODE]
- SD image written from byte 8192 (`dd seek=8` with bs=1024, i.e. sector 16). `Binaries/New software/readme.txt`: install `sudo dd if=fnirsi_1013d.bin of=/dev/sd? bs=1024 seek=8` after unmounting; remove with `sudo dd if=/dev/zero of=/dev/sd? bs=1024 seek=8 count=1` (kills the BT0 header, so BROM falls back to SPI flash).
- Layout of `fnirsi_1013d.bin` [MEASURED]: `eGON.BT0` at +0x0000 (len 0x3A00); startup-screen program `eGON.EXE` at +0x4000 (len 0x5600, SD sector 48); main app `eGON.EXE` at +0x9800 (len 0x35000, SD sector 92). The extra "eGON.EXE" at +0x38AC is a string constant inside the loader.
- Stage 1 (`Test code/fnirsi_1013d_sd_card_bootloader/fnirsi_1013d_sd_card_bootloader.c`, less than 16 KB, no FatFs): `sys_clock_init`, `sys_dram_init`, caches on, `sd_card_init`, reads sector 48, checks "eGON.EXE", loads to DRAM 0x81C00000 (4 MB for it), also reads sector 710 (display timing override) to 0x81BFFC00, jumps. Fixed sector layout is used because FatFs does not fit stage 1.
- Stage 2 (`Test code/fnirsi_1013d_startup_screen`): waits for the FPGA (cmd 0x06 must read 0x1432, FPGA takes about 900 ms to configure), shows logo, loads main app from sector 92 (`PROGRAM_START_SECTOR 92`) to 0x80000000, runs it.
- Variants: `fnirsi_1013d_startup_from_sd_card` (sector 80 to 0x80000000), `f1c100s_ch340_sd_package` (sector 48 to 0x80000000), backup/restore packages packed with `flashfilepacker -l 0x8000` (sector 80).
- Card needs FAT32 with at least 1 MiB free at the start (author used 8 GB cards; smaller cards untested).

### 2.5 Firmware backup / restore flow (`Original firmware backup/readme.txt`, `Test code/fnirsi_1013d_firmware_backup`, `_firmware_restore`)
1. In the stock firmware start USB mass storage, unmount, `dd if=fnirsi_1013d_fwb.bin of=/dev/sdX bs=1024 seek=8`.
2. Reboot: the package runs, shows progress, writes to the SD volume `FNIRSI_1013D_tp_config.bin`, `FNIRSI_1013D_full_flash_backup.bin` (full flash; block 16 KB for 2 MB parts, 32 KB when JEDEC mfr 0x20/0xEF and capacity code 0x16), and `FWB_FSI-1013.bin` (flash 0x13000 to 0x1FE100 in update-file format with check dwords, so settings and hardware-check words are included). Then it zeroes SD sector 16 (so the next boot is normal) and starts USB mass storage so the files can be copied. If `tp_config_data[29] > 0x0A` it prints "!! SPECIAL TOUCH PANEL DETECTED !!".
3. Restore (`fnirsi_1013d_firmware_restore`) reads `FNIRSI_1013D_main.bin` (written from 0x27000) or `FNIRSI_1013D_full.bin` (from 0x0) and uses `sys_spi_flash_write`. Per the README, restore failed on the author's newer scope and old-bootloader scopes lack update support. Treat as not validated.
4. 1014D: `fnirsi_1014d_firmware_backup` is the same without touch-config save; it prints flash JEDEC info and uses brightness byte 0x78.

## 3. Calibration, parameters, FPGA protocol facts for a PC interface

### 3.1 FPGA bus (full text in `FPGA explained.txt`; confirmed by the working drivers)
- PORT E (cfg 0x01C20890, ctrl cfg 0x01C20894, data 0x01C208A0): PE0-7 data, PE8 clock (acts on rising edge, idles high), PE9 R/W (0 read, 1 write), PE10 D/C (0 data, 1 command), PE11 power-off interrupt input, PE12 charger status input.
- Write command: PE9=1, PE10=1, bus out, put byte, PE8 low then high. Write data: PE9=1, PE10=0. Read data: PE9=0, PE10=0, bus in, pulse clock, read PE0-7 (value appears after the pulse). Multi-byte values are big-endian.
- Commands (selection): 0x01 sample reset/start (1/0); 0x02/0x03 CH1/CH2 enable; 0x04 system enable (=1); 0x05 ready flag, bit 0, read twice; 0x06 read version u16, must be 0x1432; 0x0A triggered/buffer-full flag bit 0; 0x0D write u32 sample-rate divisor (rate = 200 MS/s / (N+1)); 0x0E write u32 short-timebase/pre-trigger count; 0x0F trigger enable (0 on, 1 off); 0x14 read u16 trigger point (open fw: `(v & 0x0FFF) + 2`); 0x15 trigger channel; 0x16 edge; 0x17 trigger level byte; 0x1A trigger mode; 0x1F write u16 trigger point (open code adjusts +3345 or -750 around 750); 0x20/0x22 per-channel ADC1 trace command (the real command byte comes from parameter IC id 0x0C/0x0D in stock; constants `adc1command/adc2command` in open fw); 0x21/0x23 ADC2 halves (hard coded); 0x24/0x26 10-byte sum reads for slow timebases; 0x28 mode (0 below 100 ms/div, 1 from 100 ms/div up); 0x32/0x35 CH1/CH2 offset u16; 0x33/0x36 V/div select 0-5; 0x34/0x37 coupling (1 DC, 0 AC); 0x38 backlight u16 (0xEA60 max; 1014D single byte 0x78); 0x3C u16 (from param id 0x16, open code writes 32431); 0x41 read GT911 coordinate register address (0x8150); 0x64-0x6E parameter-IC frame.
- Vertical: hardware codes 0-5; 500 mV/div shares the 1 V/div code and finer ranges are software scaled; 50 mV/div is a software x2 (`Test code/fnirsi_1013d_scope/variables.c:360 signal_adjusters`).
- Rate table (`fnirsi_1013d_scope/variables.c:368 sample_rate_settings`), value written with cmd 0x0D: 0 (200 MS/s), 1 (100), 3 (50), 9 (20), 19 (10), 39 (5), 99 (2), 199 (1), 399 (500 kS/s), 999 (200 kS/s), 1999, 3999, 9999, 19999, 39999, 99999 (2 kS/s), 199999, 399999 (500 S/s). 18 entries.
- Short timebase table (`timebase_settings`, cmd 0x0E, 24 entries from 200 ms/div to 5 ns/div): 1800 (200 ms to 10 ms), 2500 (5 ms to 200 us), 3000 (100 us), 5596 (50 us), 9692 (20 us), 21980 (10 and 5 us), 83420 (2 us), 206300 (1 us), 411100 (500 ns and faster). Stock firmware used different 0x0E values (0x0645DC etc.); see the table in `FPGA explained.txt`.
- Trace read: 1500 bytes per ADC read (750 for 20 ms/div and slower in stock). Each channel is two interleaved ADC streams; open code stores ADC1 in odd and ADC2 in even slots (3000 samples per channel), applies per-ADC compensation, and computes min, max, average and zero-crossing frequency inside the read loop (`fpga_control.c:681 fpga_read_adc_data`).
- Acquisition sequence in the open code (`fpga_control.c:481 fpga_do_conversion`, `scope_functions.c:4242 scope_acquire_trace_data`): 0x0F, 0x28=0, 0x01=1, poll 0x05, 0x01=0, poll 0x0A (abort on touch), 0x0F=1; then 0x14 read; then per channel 0x1F + trigger point, ADC1 command, read 1500, 0x1F again, ADC2 command, read 1500.

### 3.2 Parameter IC (7-byte frame via FPGA commands 0x68-0x6E) [CODE]
- Write: 0x65, 0x65 (prepare write), 0x68 crypt byte, 0x69 `iiiiiicc` (6-bit id, 2-bit byte count), 0x6A checksum (sum of other six), 0x6B..0x6E value bytes MSB to LSB (unused = 0x69, 0x96, 0x99), 0x66 start, 0x67 poll bit 0. Read: 0x64, 0x64, 0x66, 0x67, then read 0x68..0x6E. Reply size descriptor 0x55/0x5A/0xA5/0xAA for 1/2/3/4-byte values. A delay is needed between write and read; the author found only that one is required (readme 02-AUGUST-2021).
- The XOR-crypt byte returned in 0x68 feeds the next frame; the first one is fetched at startup.
- Ids: 0x0A (16-bit value written to the FPGA after the command from id 0x14), 0x0B (input 0-6 returns fixed 0xAD, 0xAF, 0xB4, 0xB4, 0xB8, 0xB8, 0xB8), 0x0C / 0x0D (return the FPGA read command numbers for CH1 / CH2), 0x0E (32-bit echo), 0x10 (brightness 0-100 returns 16-bit up to 0xEA60), 0x11 (input >> 4, plus 2, 12 bits; called continuously), 0x14 (input always 0xED, returns an FPGA command byte), 0x16 (16-bit written to cmd 0x3C at start). Behaves as a translation/obfuscation chip; the emulator replays it from `scope_settings.bin`.

### 3.3 Settings and calibration block at flash 0x1FD000 (500 B = 250 little-endian u16) [CODE + MEASURED]
Index is the u16 index (byte offset = 2 x index). Verified: u16[200] at byte 0x190 reads 0x1432 in `W25Q32_scope.bin` [MEASURED]. Source: `Startup/restore_config_data.c`, `save_config_data.c`.
| idx | field |
|---|---|
| 0 to 4 | CH1 enable, volts/div, FFT enable, coupling, magnification |
| 5 to 9 | CH2 enable, volts/div, FFT enable, coupling, magnification |
| 10 | time/div (stock: 0 = 50 S, 29 = 10 ns) |
| 11 | move speed |
| 12, 13, 14 | trigger mode, edge, channel |
| 15 | right-menu state |
| 40, 41 | trigger position on screen (x), trigger level screen offset |
| 42, 43 | CH1 and CH2 screen offsets |
| 60 to 63 | screen brightness (0-100), grid brightness, always-50% trigger, x-y mode |
| 80 to 91 | CH1 measurement enables: Vmax, Vmin, Vavg, Vrms, Vpp, Vp, Freq, Cycle, Tim+, Tim-, Duty+, Duty- |
| 92 to 103 | same for CH2 |
| 120 | CH1 calibration factor (sample 0x00DC = 220) |
| 121 to 126 | CH1 per-range offsets, 6 values (sample 0x054D, 0x0545, 0x0554, 0x054D, 0x0553, 0x054C). At runtime entry 5 is copied to entry 6 (7 ranges). |
| 127 | CH2 calibration factor (sample 0x00D9 = 217) |
| 128 to 133 | CH2 per-range offsets (sample 0x055B, 0x0556, 0x0561, 0x055B, 0x0560, 0x055A) |
| 134 to ~153 | further calibration shorts and bytes, unexplained |
| 160 to 166 | time cursor enable/pos1/pos2, volt cursor enable/pos1/pos2 |
| ~167 to 190 | enabled measurement items in display order (12 pairs) |
| 200 | system-OK word, must be 0x1432 |
- The per-range offsets are what the open firmware writes with cmd 0x32/0x35 per V/div (`dc_calibration_offset[voltperdiv]`). **They are per unit. A new firmware must read and preserve them and the 0x1FE000 hardware-check word.**
- Sample block contents: `C analysis/FPGA analysis/parameter_analysis.txt` (three variants), `Binaries/Hacked files/config_data.c`. Defaults: `Startup/reset_config_data.c` (CH1 offset 300, CH2 100, trigger level 200, timebase 0x13, measurement list 5/4/7/0x11/0x13).
- Data sets for offline algorithm work: `Test code/Scope_Calibration_Analyzer/calibration_data.bin` (1,881,984 B), `Test code/Scope_Signal_Analyzer/signals/fnirsi_samples_*.bin` (400,000 B each), `Test code/Scope_Signal_Handling_Development/signals`. Their `todo.txt` lists known bad buffers (interleave mismatch, stale tails at time base index 17).

## 4. Test code projects

| Project | Language / toolchain | Target | What it is |
|---|---|---|---|
| `1014D_Emulator` | C, Linux Xlib | PC | ARM+Thumb interpreter with F1C100s peripherals, runs the 1014D flash dump; GUI with buttons and rotary dials. Needs SD check patched out. |
| `1014D_Simulator` | C, Linux Xlib | PC | Not an ARM emulator. Builds the real (private, not in this repo) open 1014D firmware C code on the PC with stubs (`stub_functions.c`, 48 KB), signal generator and UI sims. Source files of the 1014D firmware are missing here. |
| `Scope_emulator` | C, Linux Xlib, pthreads, SysV shm | PC | Emulator for the 1013D (4.1). |
| `Scope_Signal_Handling_Development` | C, Linux Xlib | PC | Hosted build of the open 1013D C code with stub FPGA/SD (`stub_functions.c`; needs `scope_sd_card.img`), virtual signal generator, USB connect via shell. Best existing harness for the processing pipeline. |
| `Arm_Trace_File_Reader` | C, Xlib | PC | Trace viewer with ARM and Thumb disassembler for emulator traces. |
| `bitmap_converter`, `fnirsi_bitmap_converter` | C | PC | Flash bitmap (RGB565 298x98) to BMP; menu bitmaps from PNG. |
| `Display_Lib_Test` | C, Xlib | PC | Develops the open display library (`display_lib.c` 41 KB, fonts 0 and 2, sin/cos); `todo.txt` is arc-drawing scratch. |
| `F1C100s_Test_Code` | C, arm-none-eabi | board via FEL | Bring-up: FPGA backlight on, GT911 I2C read loop, PORT A toggle every 100 us. Three prebuilt bins in `Binary test files/`. |
| `f1c100s_ch340` | C and asm | board | USB CH340 emulation with text command interpreter for raw FPGA access (4.2). |
| `f1c100s_ch340_sd_package` | C | SD | BT0 loader (DRAM init, SD read, jump) for the CH340 image. |
| `flashfilepacker` | C | PC | Update/SD image builder with the 9 check dwords (1.4). |
| `fnirsi_1013d_bootloader` | C | replaces SPL | Reimplemented SPL: clock, DRAM, SPI, display, FPGA, bitmap from 0x13000, main app from 0x27020 to 0x80000000. 9,728 B. |
| `fnirsi_1013d_sd_card_bootloader` | C | SD BT0 | Stage 1 SD loader (2.4). |
| `fnirsi_1013d_startup_from_sd_card` | C | SD | Waits for FPGA, loads program from sector 80. |
| `fnirsi_1013d_startup_screen` | C | SD | Stage 2: logo, loads main from sector 92. |
| `fnirsi_1013d_startup_with_fel` | C | SD | Init then return to BROM FEL (`pc = 0xFFFF0020`). 1014D twin: `fnirsi_1014d_startup_with_fel` (backlight byte 0x78). |
| `fnirsi_1013d_firmware_backup`, `_firmware_restore`, `fnirsi_1014d_firmware_backup` | C and FatFs | SD | Section 2.5. |
| `fnirsi_1013d_original_scope` | C | board | Earlier line of the open firmware that keeps the stock structure (`scope_functions.c` 252 KB, `statemachine.c` 94 KB, `todo.txt`). |
| `fnirsi_1013d_scope` | C | board | Latest snapshot. Builds in `dist/Debug/GNU_ARM-Linux/`: `FSI-1013.bin` 304,200 (update file), `fnirsi_1013d.bin` 261,192 (SD image), `fnirsi_1013d_scope.bin` 222,208 (FEL), ELF 914,844. |
| `fnirsi_sd_card_bootloader` | C | SD | Older bootloader with its own fonts; superseded. |
| `FNIRSI_Font_Extract`, `Scope_fonts` | C | PC | Font extractor (flash from 0x27000) and results. |
| `i2c_bin_data_converter` | C | PC | Decodes Saleae Logic 2 exports of the FPGA to parameter-IC bus into command streams and checks checksums (3 captures, one from e_Johny's startup trace). |
| `initial_data_unpacker` | C | PC | Unpacks the 42,756-byte init-data blob. |
| `FPGA_functions` | C | | Reference copy of FPGA driver. |

### 4.1 Emulator details (candidate test harness) [CODE]
- `Scope_emulator` (1013D) and `1014D_Emulator` (fork). Linux only: Xlib, pthreads, `shmget` shared memory. GUI: scope panel with mouse touch, and an ARM status window (registers, stack, disassembly, memory, breakpoints). Trace output: `fpga_trace/fpga_trace_a_NNNNNN.txt` logs every FPGA command with data (5000 lines per file), optional `param_trace/`.
- CPU: ARMv5TL ARM and Thumb interpreter (`armv5tl.c` 83 KB, `armv5tl_thumb.c`), CP15 control register only (no MMU), IRQ only, `clz`, multiplies. Known issue: at -O1 rounded rectangles were drawn wrong in the emulator but fine on hardware (suspected multiply instruction bug, `fnirsi_1013d_original_scope/todo.txt`).
- Peripheral map (`1014D_Emulator/armv5tl.c:799`) implemented: SRAM1 0x0-0x7FFF, SRAM2 0x10000-0x19FFF, DRAMC, SPI0 (backed by the flash image file), TCON, CCU (reports PLL locked), INTC, PIO (PORT E carries the FPGA model), TIMER (TMR0 1 ms), UART0 and UART1 (1014D front-panel byte feed on UART1), DEBE (framebuffer shown in window), DRAM 0x80000000-0x81FFFFFF. Stubs (NULL): SD/MMC0, USB-OTG, DMA, KEYADC, TWI, PWM, audio, CSI, DEFE.
- FPGA model (`Scope_emulator/f1c100s_pio.c:501 PortEHandler`): state machine on the clock rising edge tracking command/data and read/write; returns canned `scope_ch1_data` / `scope_ch2_data` (1500 B) for 0x20 / 0x22; 0x14 constant `07 D5`; 0x0A returns 1; 0x41 returns 0x8150; the parameter IC is emulated including checksum and XOR crypt, backed by `scope_settings.bin` (260 B). **No trigger, timebase or analog model.**
- Reuse assessment [INFER]: good as a bus-protocol conformance checker, instruction tracer, and for running the unmodified stock binary to capture FPGA command sequences. Weak as a closed-loop model (no signal model, timing not cycle accurate, X11 GUI only, no headless mode, no USB so a PC interface cannot be exercised). For MCP-interface testing the hosted simulators (`Scope_Signal_Handling_Development`, `1014D_Simulator`) fit better; the emulator's `PortEHandler` is the natural place to grow a table-driven FPGA model.
- Dated emulator discoveries: `Test code/Scope_emulator/readme.txt` entries 09-JUNE-2021 and 12-JUNE-2021 (settings block at 0x1FD000, channel commands, V/div tables).

### 4.2 `f1c100s_ch340`: USB device on the F1C100s MUSB core (most relevant for a PC interface) [CODE]
Files: `usb_interface.c/.h` (826 lines; register map, PHY setup, EP0 and IRQ), `ch340_class.c/.h` (descriptors, ring buffers, endpoint callbacks), `commands.c/.h` (parser), `fpga_control.c/.h`, `f1c100s_ch340.c` (main at line 34), `interrupt.c`, `timer.c`, `ccu_control.c`, `display_control.c`, `start.s`, `fnirsi_1013d.ld`.
- Hardware: MUSB core at 0x01C13000 (FIFO ports 0x01C13000 + 4n, common regs from +0x40, indexed endpoint regs from +0x80, Allwinner PHY regs at 0x01C13400 ISCR/PHYCTL, 0x01C13800 PMU IRQ), INTC IRQ 26, priority 2. USB FIFO RAM base selected by writing SYS_CNTRL 0x01C00004 low bits = 1. PHY tuning via the 0x1C13410 serial interface (`usb_phy_setup`): (0x0C, 1, 1 bit) 45 ohm regulation, (0x20, 0x14, 5 bits) magnitude/rate, (0x2A, 3, 2 bits) disconnect threshold. D+/D- pull-ups on, ID forced device, VBUS forced valid. `VEND0 = 0` selects PIO mode, so **no DMA**.
- Enumeration as a CH340: device 0x1A86:0x7523, release 0x0263, class 0, EP0 64 B, USB 2.0, one configuration. Strings: manufacturer "STMicroelectronics" (indexes 1, 3, 0xEE), product and serial "USB2.0-Serial". Config: 1 interface class 0xFF / 1 / 2, 3 endpoints: 0x82 bulk IN 512, 0x02 bulk OUT 512, 0x81 interrupt IN 8 B interval 1. Vendor requests: 0x5F returns `31 00`; 0x95 with wValue 0x2518 returns `C3 00`; 0x95 with wValue 0x0706 or 0x0005 returns `FF EE`. That is enough for the stock Linux `ch341`/Windows CH340 drivers. High speed is detected from the POWER register; full-speed hosts are not handled (code comments say the max packet size is not adjusted).
- Endpoint setup (at `USB_REQ_SET_CONFIGURATION`, `usb_interface.c:586`): EP1 interrupt TX: MAXP 8, FIFO size code 6 with double-buffer bit, FIFO address 256 (units of 8 B). EP2 bulk RX: MAXP 512, size code 6 with DPB, address 0. EP2 bulk TX: MAXP 512, size code 6 with DPB, address 128. Interrupts enabled: RX EP2, TX EP1 and EP2, and misc SOF, SUSPEND, RESUME, RESET, DISCONNECT.
- Data path: RX interrupt reads the FIFO (32-bit reads when the destination is aligned) into a 512 B `fifobuffer`, then copies bytes into the 1024 B ring `usb_rx`; the main loop polls `ch340Receive()` (returns -1 when empty). TX: `ch340Send(c)` busy-waits while the 1024 B ring `usb_tx` is full; `usb_ch340_in_ep_callback` is called from the SOF interrupt and from the EP2 TX-complete interrupt, copies up to 512 B to `fifobuffer`, then `usb_write_ep2_data` (32-bit FIFO writes, clears underrun, sets TX_READY). `EP2DisableTX` guards ring updates. All single threaded, PIO, double copy, per-byte ring handling, 1 KB buffers.
- Throughput hints [INFER, no measurement in the repo]: high-speed bulk 512-byte packets; limits are PIO copies, the extra copy through `fifobuffer`, the per-byte `ch340Send`, and TX being kicked from SOF and TX-done. A few MB/s is the realistic order, ample for 6000-byte captures, but a 64-char text protocol adds parsing cost and a 768 KB screenshot would take a noticeable fraction of a second.
- Command interpreter (`commands.c`, `commands.h`): line editor and tokenizer (LINE_LENGTH 64, MAX_TOKENS 4, case-insensitive via `& 0x5F`, `0x` hex, error codes 1 to 12), table `variablelist[]` with 7 entries: `WC=n` (write FPGA command byte 0-255), `WB=n` (write data byte), `WS=n` (u16), `WI=n` (u32), `RB`, `RS`, `RI`. Line terminated by CR or LF. Exposes only the raw FPGA bus; a good template, to be replaced by framed commands for an MCP bridge.
- Loading: FEL (`sudo ./sunxi-fel -p write 0x7FFFFFE0 f1c100s_ch340.bin exe 0x80000000`; main waits 200 ms so FEL's USB disconnects first) or SD (`f1c100s_ch340_sd_package.bin`, `dd ... seek=8`; remove with the zero-sector `dd`). Prebuilt: `dist/Debug/GNU_ARM-Linux/f1c100s_ch340.bin` 12,800 B, `_sd_package.bin` 29,256 B.
- Open scope firmware USB is mass storage only (`fnirsi_1013d_scope/mass_storage_class.c`, 603 lines; VID/PID 0x0483/0x5720, BOT, bulk 512, SCSI INQUIRY, READ_CAPACITY_10, READ_10, WRITE_10, MODE_SENSE_6, TEST_UNIT_READY, START_STOP, PREVENT_ALLOW; thumbnail buffer reused as SCSI buffer, `SCSI_MAX_BLOCK_COUNT`). It shares the MUSB layer in `usb_interface.c` with the CH340 project.

## 5. Platform facts

### 5.1 Clocks, memory, interrupts
- CPU PLL 600 MHz: PLL_CPU_CTRL = enable plus N=25 (24 MHz x 25) (`Clock analysis/FUN_80028028.c`, `Startup/sys_clk_init.c:23`, `Test code/f1c100s_ch340/ccu_control.c`). Code comment states HCLK 300 MHz, AHB 300 MHz, APB 150 MHz from `CCU_HCLKC_DIV_2 | AHB_SRC_CPU | APB_RATIO_2 | AHB_DIV_2` (I did not re-decode the register). Peripheral PLL 576 MHz (N=24) so SD can run 48 MHz. Video PLL 390 MHz ((24 x 65) / 4); LCD dot clock /6 = 65 MHz (`TCON_CLK_CTRL 0xF0000006`). Delay calibration used in code: 1000 loop iterations is about 200 us at 600 MHz; `fpga_delay(usec)` loops `usec * 54`.
- DRAM: DDR, 32 MB, 16-bit bus, DDR PLL 156 MHz, columns 10, rows 13, 4 banks, CAS 3 (`Test code/fnirsi_1013d_bootloader/dram_control.c`). Peak ~624 MB/s, shared with scan-out (800x480x2 B at about 58 Hz, ~45 MB/s) and the CPU. Caches on, MMU off, so peripherals and DRAM are physically addressed; no cache maintenance code exists in the display or USB code.
- SRAM: 32 KB at 0x0 (SPL); SRAM2 at 0x10000 (USB FIFO related region).
- Timer: TMR0 clocked from OSC24M, reload 24000, 1 ms interrupt (IRQ 13); watchdog at 0x01C20CB0 is used for reset.
- INTC at 0x01C20400: pending 0x10/0x14, enable 0x20/0x24, mask 0x30/0x34, priority from 0x60. Used: IRQ 13 TMR0 (prio 0), IRQ 26 USB OTG (prio 2), IRQ 0x27 = 39 PIO port E external (power-off, prio 3). Handler table of 64; `irq_handler` (`Test code/*/interrupt.c`) walks pending words and clears them before servicing. No nesting.
- Peripheral bases seen: SYSCTL 0x01C00000, DRAMC 0x01C01000, SPI0 0x01C05000, TCON0 0x01C0C000, SDC0 0x01C0F000, USB-OTG 0x01C13000, CCU 0x01C20000, INTC 0x01C20400, PIO 0x01C20800, TIMER 0x01C20C00, KEYADC 0x01C23400, UART0 0x01C25000, UART1 0x01C25400, DEFE 0x01E00000, DEBE 0x01E60000.

### 5.2 GPIO usage
| Port | Pins | Use |
|---|---|---|
| PA | 0-3 | touch panel: PA0 RST, PA1 INT, PA2 SDA, PA3 SCL (bit-banged). Alt function UART1 RX/TX (the Linux console trick). |
| PB | 3 | config bits 0x7000 set during DRAM init |
| PC | 0-3, function 2 | SPI0 CLK, CS, MISO, MOSI to the NOR flash. Alts: PC3 UART0 TX, PC0-2 SDC1. |
| PD | 0-21, function 2 | LCD RGB data/clock/sync (TCON0) |
| PE | 0-7 | FPGA data bus (PE0/PE1 also UART0) |
| PE | 8, 9, 10 | FPGA clock, R/W, D/C |
| PE | 11 | power-off interrupt input |
| PE | 12 | charger status input |
| PF | 0-5, function 2 | SD card SDC0 |
- SPI0: clock = AHB/6 in the open code; comment in `fnirsi_1013d_scope/spi_control.c:27` says the stock 1014D uses divider 5. Master mode, software chip select, 64-deep FIFO, polled.

### 5.3 Display
- 800x480 RGB565. DEBE layer 0 RGB565, line width = xsize x 16 bits, framebuffer address split across two registers. TCON0: `TIMING1 0x041E004A`, `TIMING2 0x041A0017`, `TIMING3 0x160000`, `IO_CTRL0 0x17000000`. Five panel variants across hardware revisions (`fnirsi_1013d_scope/display_control.c:176`): 1014D new 0x041E0043 / 0x041A0019; 1014D old 0x041E006D / 0x041A0022; 1013D v1 0x041E0044 / 0x041A0017; v2 0x041E004A / 0x041A0017 (most common); v3 0x041E0072 / 0x041A001F. The open code accepts an override from SD sector 710 (magic AAAAAAAA 55555555, timing1, timing2, CCCCCCCC 33333333, checksum).
- Backlight: set by the FPGA, cmd 0x38 + u16 (0xEA60 = 100%). Open code maps `value = brightness * 560 + 4000`.

## 6. Linux (`Linux/`)
- What it is: a Buildroot setup for a Lichee Pi Nano (F1C100s) adapted to the scope. Buildroot 2021.02 ("latest, 2021.2"), glibc toolchain, soft-float ARM926EJ-S, kernel `Lichee-Pi/linux` branch `nano-5.2-tf` (5.2), U-Boot `Lichee-Pi/u-boot` `nano-v2018.01`, genimage SD image (U-Boot SPL at 8 KiB, 8 MiB FAT boot partition with zImage, DTB and `boot.scr`, 10 MiB ext4 root). Files: `buildroot/configs/licheepi_nano_defconfig`, `buildroot/board/licheepi_nano/{boot.cmd, genimage.cfg, licheepi_nano_linux_defconfig, suniv-f1c100s-licheepi-nano-lcd480.dts, post-image.sh}`, patched DT includes `buildroot/Files to change in linux source tree/{suniv-f1c100s.dtsi, suniv.dtsi}`, instructions in `buildroot/readme.txt`.
- Images (each directory: `sdcard.img` 19,922,944 B, `zImage` ~3.73 MB, `u-boot-sunxi-with-spl.bin` 548,864 B, `boot.vfat` 8 MiB, `rootfs.ext2/.ext4` 10 MiB, DTB ~6 KB): `lichee_nano_both_uart1` (U-Boot and Linux on UART1), `lichee_nano_both_uart1_backlight_on` (U-Boot also waits for FPGA 0x1432 and sends cmd 0x38 = 0xEA60), `lichee_nano_both_uart1_fail` (UART1 only; U-Boot console fails), `lichee_nano_uboot_uart0_linux_uart1` (with wiring photo and readme), `lichee_nano_uart1_innolux,at070tn92` (tar.gz 16.3 MB). `changed packages/*/uboot-nano-v2018.01-br1.tar.gz` (~15 MB each) are the patched U-Boot trees; the backlight variant adds `fpga_control.c/.h` and `CONFIG_FNIRSI_1013D_BACKLIGHT` in `arch/arm/mach-sunxi/board.c`.
- Boot script: `setenv bootargs console=tty0 console=ttyS0,115200 panic=5 rootwait root=/dev/mmcblk0p2 rw; load mmc 0:1 0x80C00000 <dtb>; load mmc 0:1 0x80008000 zImage; bootz 0x80008000 - 0x80C00000`.
- How to boot on the scope: `dd` `sdcard.img` to an SD card and insert it; BROM runs U-Boot SPL from SD. The serial console must use **UART1 on PA2 (RX) / PA3 (TX)**, which on the scope are the touch panel SDA/SCL, so the adapter is wired to the touch FPC pins 5 and 6 and touch is unusable meanwhile. UART0 (PE0/PE1) is unusable because it is the FPGA bus. `Linux/images/lichee_nano_uboot_uart0_linux_uart1/readme.txt` has the pin mapping.
- What works [CODE + INFER]: boot, DRM display (sun4i, simple-panel `innolux,at070tn92` on TCON0 RGB666 pins PD0-21), SD (mmc0, 4-bit, `broken-cd`), UART1 console. Backlight needs the FPGA command from U-Boot. Kernel has MUSB/USB gadget, SPI (`SPI_SUN6I`, `SPIDEV`, `MTD_M25P80`) and Goodix touch (module) enabled but none are shown to work on the scope. Warning: the DT panel node uses `enable-gpios = <&pio 4 6>`, which is **PE6, an FPGA data line**.
- **Not present**: any driver or userspace for the FPGA bus (needs PE0-PE12 GPIO fast path), touch (pin conflict with the console), DMA use, any scope application.
- Viability as the scope platform [INFER]: poor. 32 MB RAM leaves roughly 20 MB for the application after kernel and rootfs. FPGA capture is a CPU-bound bit-bang loop and would suffer from kernel jitter; GPIO via sysfs is far too slow, so a custom kernel module or memory-mapped userspace access with interrupts masked would be needed. The shared pins make bring-up awkward. Benefits: free USB gadget (CDC-ACM/RNDIS), network stack, filesystems. Bare-metal already works with a ~220 KB image. Use Linux only as a lab tool if at all.

## 7. Performance observations (where time goes)
1. **FPGA reads are bit-banged GPIO.** Per byte: clock low, clock high, read, i.e. read-modify-write on the PE data register plus a read, about 3 APB accesses. Stock code toggles the 8 data pin directions via `gpio_config_pin` per call (`FUN_800167A0`, 8 calls per direction change). The open code sets the bus direction once per block (`fpga_read_adc_data`, `fpga_control.c:681-860`) but still uses `&=` and `|=` on the data register per sample. A full capture is 2 ADCs x 2 channels x 1500 = 6000 byte reads, ~18,000 PIO accesses. Write-only toggles with precomputed constants, an unrolled loop, and moving min/max/average/zero-crossing into a second pass would help most. No timing measurement exists in the repo.
2. **No DMA in user firmware** (no use of the 0x01C02000 block; the emulator maps it NULL). The stock SD driver uses DMA for large blocks; the open code does not. Because the FPGA clock is produced by software, DMA cannot move FPGA samples unless the wiring or the FPGA changes [INFER].
3. Per-sample processing (compensation, clamp, min/max/average, frequency state machine) runs inside the read loop and lengthens the bus access. Stock also stores each sample in a 16-bit slot.
4. Parameter IC access has long fixed delays in stock; the author showed only the delay between write and read is needed (readme 02-AUGUST-2021).
5. Touch panel is polled by software: 228 call sites in stock, and from inside capture wait loops (`fpga_do_conversion` polls while waiting for trigger); I2C bit-bang at ~200 kHz after tuning.
6. Display: scan-out buffer written directly in stock, causing flicker when moving traces; open code builds in off-screen buffers (`displaybuffer1`, `displaybuffer2`) and copies with `memcpy` (`display_lib.c:1060 display_copy_rect_to_screen`). Rectangle fill is still a per-pixel 16-bit store loop (`todo.txt`: "could do with speed improvement on filling with a color"). A full frame copy is 768 KB.
7. Main loop is strictly serial: acquire, process, draw, touch. No timer-driven or interrupt-driven acquisition; USB and scope run are mutually exclusive in the stock design.
8. Long timebases (100 ms/div and up) use timer-driven roll mode with 10-byte sum reads; timeouts are "error prone on timer overflow" (`Main loop/trace data processing/process_trace_data.c:100`).
9. Caches are on without cache maintenance; fine while nothing uses DMA, but any DMA or USB-DMA buffer needs coherence handling.

## 8. 1014D differences shown in the repo
- Second MCU **GD32E230** (Cortex-M23) drives the front panel (buttons, rotary dials): SVD `Software reverse engineering/1014D/CMSIS_SVD/GD32E230_plus_core_perihperals.svd` (815,842 B) and Ghidra archive `Software reverse engineering/1014D_GD32E230.gar` (139,791 B). The F1C100s reads single bytes from **UART1** (0x01C25400); the emulator injects them (`1014D_Emulator/f1c100s_uart.c`). The protocol bytes themselves are not documented [NOT FOUND].
- No touch panel code in the 1014D backup tool.
- Backlight command 0x38 takes one byte (default 0x78) instead of u16 0xEA60.
- Display timing variants (5.3), SPI divider 5 per comment, flash 2 MiB and 4 MiB variants (JEDEC check), FPGA flash `ST25P16VP` (old) or `W25Q80` (new).
- Trigger limits noted in `fnirsi_1013d_original_scope/todo.txt`: in 1014D single trigger the longest time base is 10 ms/div and in normal trigger 50 ms/div (author asked to verify if the same on 1013D).
- Main app is much smaller (0xBC400 vs 0x193A00).
- Combined Ghidra archive `1013D_1014D_scope_armv5tej.gar` (8,683,818 B; log: 16-Nov-2021 combined, 31-May-2022 SVD added).

## 9. Ghidra material and C extracts
- `Software reverse engineering/C output from Ghidra/`: `SPL.c`, `1st_executable_part_c.c/.h`, `2nd_executable.c` (1.6 MB), `2nd_executable_part_c.c/.h` (raw decompiler output). `asm output from Ghidra/` has crude and refined listings (the 2nd-exec ones are ~5 MB zips). `Analysis/Sub parts SPL` and `Sub parts 2nd executable` are annotated function listings (GPIO/port setup call trees; `Port A setup.txt` lists touch/I2C call tree).
- `C analysis/` is cleaned per-function C by topic (Auto setup, Calibration, check hardware, Clock, Display, FPGA, GPIO, I2C, Main loop with touch and trace subfolders, SD and FLASH, SPL, Startup, UART, USB). Readable references, not compilable.

## 10. Implications for a PC / MCP control interface on the open firmware
- Base: `Test code/fnirsi_1013d_scope`. Settings live in a `scopesettings` struct (`variables.h`); every FPGA write goes through `fpga_set_*` (`fpga_control.c`); acquisition is `scope_acquire_trace_data()` (`scope_functions.c:4242`) producing per-channel `tracebuffer` of 3000 samples plus min, max, average and frequency; main loop at `fnirsi_1013d_scope.c:151`; touch handling in `statemachine.c:25`; config load/save at `scope_functions.c:7615` and `:7684`; auto set at `:4665`.
- USB: only mass storage exists. A CDC/vendor-bulk class can be added by reusing the endpoint setup from `f1c100s_ch340/usb_interface.c` (EP2 bulk IN/OUT double-buffered 512, EP1 interrupt) and swapping descriptors. CH340 IDs 0x1A86:0x7523 bind without custom drivers on common hosts; the scope's mass-storage ID is 0x0483:0x5720.
- The existing text protocol exposes raw FPGA access only. A PC interface needs settings get/set (1:1 with `scopesettings`), run/stop/single, trace fetch (up to 6000 B), screenshot (768,000 B) endpoints, ideally framed binary bulk.
- Open question: nobody has run USB (interrupt-driven) concurrently with the capture bit-bang loop. USB IRQ jitter during the read loop must be bounded or the loop must mask it.
- Do not break: settings block at flash 0x1FD000 (per-unit calibration), hardware-check word at 0x1FE000, FPGA bring-up wait (cmd 0x06 equals 0x1432, about 900 ms).
