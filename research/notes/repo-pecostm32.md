# pecostm32/FNIRSI_1013D_Firmware digest

Repo: D:\src\FNIRSI_1013D_Firmware (HEAD 51f4872 "Update README.md").
Path shorthand: `S/` = `fnirsi_1013d_scope/`, `BL/` = `fnirsi_1013d_sd_card_bootloader/`, `SS/` = `fnirsi_1013d_startup_screen/`, `NS/` = `fnirsi_1013d_startup_from_sd_card/`. A bare `file.c:123` inside a section refers to the `S/` copy unless stated.
"INFERRED" = my reading, not stated in code. "UNKNOWN" = not in repo.

Scope caveats up front:
- Firmware is for the 1013D only (README.md:9, not 1014D).
- There is NO signal generator code anywhere (grep for generator/pwm/siggen finds nothing). The only PWM-like output is the LCD backlight (FPGA cmd 0x38). A "control the signal generator" feature has no upstream support.
- There is no UART, JTAG or semihosting debug support.

---------------------------------------------------------------------------------------------------
## 0. Repo contents and versions
- `README.md`: install instructions (dd to SD at 8 KiB), project overview, version history. Points to a later fork by Atlan4 (README.md:1-4, https://github.com/Atlan4/Fnirsi1013D, "latest firmware version"), which is NOT in this clone.
- `versions.txt`: v0.002 initial; v0.003 measurements; v0.004 touch XY swap in display cfg; v0.005 (2022-03-09) auto-setup returns to AUTO trigger, full screen initially; v0.006 (2023-01-12) RMS fix by ziutek.
- `V0.005_Windows.7z`: unverified third-party image (README.md:63).
- `configuration_file.txt`, `waveform_file_format.txt`: see 2.6 and 2.7.
- `power_points.jpg`, `power_points_2.jpg`: photos (not inspected).
- Prebuilt binaries: `S/dist/Debug/GNU_ARM-Linux/fnirsi_1013d.bin` (271944 B full SD image), `fnirsi_1013d_scope.bin` (232960 B), three 512 B display/touch config sectors; `BL/dist/.../fnirsi_1013d_sd_card_bootloader.bin` (14848 B); `SS/dist/.../fnirsi_1013d.bin` (38472 B) and `fnirsi_1013d_startup_screen.bin` (22016 B).
- Prebuilt x86-64 Linux ELF helpers, no source here: `mksunxi` (fixes eGON header checksum/length) and `flashfilepacker` (`-o out -i in -l offset`). Packer source lives in https://github.com/pecostm32/FNIRSI-1013D-Hack (README.md:61).
- Size: ~51k lines, of which FatFs (`ff.c` 8756, `ffunicode.c` 15593) and fonts/icons dominate. Own logic: `scope_functions.c` 8229, `statemachine.c` 2970, `display_lib.c` 1678, `sd_card_interface.c` 1274, `fpga_control.c` 908, `usb_interface.c` 826, `mass_storage_class.c` 603, `touchpanel.c` 538, `spi_control.c` 448.

---------------------------------------------------------------------------------------------------
## 1. Hardware as documented in code

### 1.1 SoC and clocks
- SoC: Allwinner F1C100s, ARM926EJ-S, no FPU (`-mcpu='arm926ej-s' -mfloat-abi=soft`, S/nbproject/Makefile-Debug.mk). SRAM at 0x0 (boot stages link there, BL/fnirsi_1013d.ld:2). DRAM 32 MiB DDR at 0x80000000 (BL/dram_control.c:9-20: 32 MB, 16-bit bus, DDR, 10 col, 13 row, 4 banks, CAS 3, 1 CS).
- `ccu_control.c:8-38` `sys_clock_init()`:
  - PLL_CPU = 24 MHz x N(25) = 600 MHz (comment ccu_control.c:16).
  - AHB/APB: comment says HCLK 300 MHz, AHB 300 MHz, APB 150 MHz (`CCU_HCLKC_DIV_2 | AHB_SRC_CPUCLK | APB_RATIO_2 | AHB_DIV_2`, :13-14). Not verified.
  - PLL_PERIPH = 24 x 24 = 576 MHz (:30) so the SD clock can be 48 MHz.
  - PLL_DDR = 156 MHz (BL/dram_control.h:39).
  - PLL_VIDEO = 24 x 65 / 4 = 390 MHz (display_control.c:118-119).
- Timer0: 1 ms tick from OSC24M, reload 24000 (timer.c:7-27). `timer0_get_ticks()` / `timer0_delay(ms)` (timer.c:34-50), uint32 ticks, wraps in ~49 days. Timer1/2, AVS and watchdog registers are defined but unused.
- Interrupts: `interrupt.c` has a 64-entry handler table, two pending registers, `setup_interrupt(irq, fn, prio)`. IRQs used: TMR0 = 13 (prio 0), USB = 26 (prio 2, usb_interface.c:99), Port E EINT = 0x27 (prio 3, power_and_battery.c:116). IRQ entry `start.s:111-133`; vectors copied to address 0 (`start.s:63-79`); FIQ, abort, undefined, SWI are `b .` hangs.
- Caches: only `arm32_icache_enable(); arm32_dcache_enable();` (fnirsi_1013d_scope.c:44-46). The MMU is never enabled (`arm32_mmu_enable` exists at arm32.h:59, no page tables are built). On ARM926EJ-S data-cache cacheability comes from MMU page tables, so the D-cache is probably ineffective and DRAM accesses may be uncached. HYPOTHESIS to test for performance.
- Stacks (`start.s:143-155`): und 0x81FB5000, abt 0x81FC1800, irq 0x81FDA800, fiq 0x81FE7000, svc 0x81FF3800, sys 0x82000000.

### 1.2 GPIO map as used in code
| Pins | Function | Source |
|---|---|---|
| PA0..PA3 | Touch panel: PA0 RESET, PA1 INT, PA2 SDA, PA3 SCL, bit-banged I2C | touchpanel.c:1-12 |
| PB3 | disabled during DRAM init | BL/dram_control.c:31 |
| PC0..PC3 | SPI0 to NOR flash: CLK, CS, MISO, MOSI (function 2) | spi_control.c:13-14, gpio_control.h:26-47 |
| PC3 | alt function UART0_TX defined, unused | gpio_control.h:49 |
| PD1..PD21 (not PD0, PD12) | LCD parallel RGB signals (function 2): CFG0 0x22222227, CFG1 0x22272222, CFG2 0x00222222 | display_control.c:57-59 |
| PE0..PE7 | FPGA 8-bit bidirectional data bus (PE_CFG0 0x01C20890: 0 in, 0x11111111 out) | fpga_control.h:14,34-35 |
| PE8 | FPGA clock strobe (idle high) | fpga_control.h:22,31 |
| PE9 | FPGA write/read select (1 = write) | fpga_control.h:25-28 |
| PE10 | FPGA command/data select (1 = command) | fpga_control.h:25-28 |
| PE11 | power-off interrupt input (EINT) | power_and_battery.c:103-117 |
| PE12 | charge indicator input (low = charging) | power_and_battery.c:49-61 |
| PF0..PF5 | SD card SDC0, CFG 0xFF222222 | sd_card_interface.c:45 |
| KEY ADC 0x01C23400 | battery voltage | power_and_battery.c:12-13 |
| USB0 OTG | PHY used as device | usb_interface.c |
Control line decode (PE_DATA bits 10:9, mask 0x600): CMD_WRITE 0x600, CMD_READ 0x400, DATA_WRITE 0x200, DATA_READ 0x000 (fpga_control.h:25-28).

### 1.3 Display
- 800x480 RGB565. DEBE layer 0 scans `maindisplaybuffer` (768000 B, variables.c:57) into TCON0 parallel RGB (display_control.c:49-221).
- Dot clock: TCON_CLK_CTRL = 0xF0000006, so 390 MHz / 6 = 65 MHz (:143-144). Defaults HT/HBP 0x041E0044, VT/VBP 0x041A0017, HSPW reg 0x160000 (:204-205). Using the F1C100s register layout this is about 1055 x 525 clocks, roughly 117 Hz scan-out (my arithmetic, INFERRED).
- Panel timing is read from DRAM 0x81BFFC00 (display_control.c:13), filled by the bootloader from SD sector 710 (BL .c:19,85). Validated with preamble, postamble and a 6-word sum (display_control.c:149-166). Touch swap bytes at 0x81BFFC1C (touchpanel.c:16).
- Panel variants (display_control.c:180-186): 1014D new 0x041E0043/0x041A0019; 1014D old 0x041E006D/0x041A0022; 1013D v1 0x041E0044/0x041A0017; v2 0x041E004A/0x041A0017 (most common); v3 0x041E0072/0x041A001F.
- Backlight: FPGA PWM, cmd 0x38 plus 16-bit value (fpga_control.c:176-190). Setting 0..100 maps to `b*560+4000`, so 100 = 60000 = 0xEA60, 0 = off.

### 1.4 Touch panel
- GT911/GT9157 family, 7-bit address 0x14 (write 0x28, read 0x29), 16-bit register addresses. Start-up uses INT level to pick the address (touchpanel.c:117-143). Regs: CMD 0x8040, CFG_VERSION 0x8047, STATUS 0x814E, COORD1 0x8150 (touchpanel.h:12-16).
- Polled, not interrupt driven. Single touch point only (status & 0x0F == 1, touchpanel.c:215-250). Coordinates scaled by 1<<20 fixed-point scalers derived from the panel's own config xmax/ymax (touchpanel.c:151-160). Mirror flags from the config sector (xswap/yswap == 0 means mirror, :232-242).
- `tp_delay` busy loop of usec*90 iterations; below 64 the panel loses configuration (touchpanel.c:522-535). Bit-banged transactions are slow, and `fpga_do_conversion` calls them while waiting for a trigger.
- Compile options `USE_TP_CONFIG` (canned GT9157 config, touchpanel.c:20-60) and `SAVE_TP_CONFIG` (dump panel config to `FNIRSI_1013D_tp_config.bin`, scope_functions.c:8214-8227). Neither is defined by default.

### 1.5 FPGA interface (central for PC control and performance)
Physical: 8-bit bus PE0-7, strobe PE8, PE9 write/read, PE10 command/data. Synchronous bit-bang with no explicit delays (`fpga_control.h:13-36`, implementation `fpga_control.c:11-174`).

Primitives:
| Op | Lines | Behaviour |
|---|---|---|
| `fpga_write_cmd(b)` | c:22-34 | PE9=1, PE10=1, bus out, put byte, pulse PE8 low then high |
| `fpga_read_cmd()` | c:36-54 | bus in, PE9=0, PE10=1, pulse, read (unused in practice) |
| `fpga_write_byte(b)` | c:56-70 | PE9=1, PE10=0, one pulse |
| `fpga_read_byte()` | c:72-88 | PE9=0, PE10=0, pulse, then read PE0-7 |
| `fpga_write_short(w)` | c:90-111 | two data writes, MSB first |
| `fpga_read_short()` | c:113-139 | two reads, MSB first |
| `fpga_write_int(d)` | c:141-174 | four data writes, MSB first |
A register write is: write_cmd(cmd) then N data bytes. A register read is: write_cmd(cmd) then N read_byte. Streaming reads (`fpga_read_adc_data`, c:705-885) set the control lines once, then per sample do `PE8 &= ~0x100; PE8 |= 0x100;` and read PE0-7: one strobe per sample byte.

Complete command table seen in code:
| Cmd | Dir | Data | Meaning | Where |
|---|---|---|---|---|
| 0x01 | W | 1 byte (1 reset, 0 run) | reset/arm the sample system | c:498-512 |
| 0x02 | W | 0/1 | CH1 enable | scope_functions.c:7829 |
| 0x03 | W | 0/1 | CH2 enable | scope_functions.c:7855 |
| 0x04 | W | 1 (always) | "enable system", unexplained | c:224-229 |
| 0x05 | R | 1 byte, bit0 | ready after reset; polled until 1, then polled again ("glitch?") | c:502-509 |
| 0x06 | R | 16-bit | FPGA version, boot waits for 0x1432 | c:197-221 |
| 0x0A | R | 1 byte, bit0 | triggered / buffer full, polled | c:515-537 |
| 0x0D | W | 32-bit | sample-rate divider, 200 MHz / (v+1) (table variables.c:364) | c:301-320 |
| 0x0E | W | 32-bit | "short time base" value from `timebase_settings` (variables.c:447), semantics unknown | c:463-475 |
| 0x0F | W | 1 byte | trigger gate: 0 = trigger active (samplemode 1), 1 = free-run/disabled | c:478-535 |
| 0x14 | R | 16-bit (low 12 bits) | trigger / write pointer after capture, code adds 2 | c:541-565 |
| 0x15 | W | 1 byte | trigger source: 0 CH1, 1 CH2 | c:323-341 |
| 0x16 | W | 1 byte | trigger edge: 0 rising, 1 falling | c:343-361 |
| 0x17 | W | 1 byte | trigger level 0-255, 128 = centre | c:397-441 |
| 0x1A | W | 1 byte | trigger mode: 0 auto, 1 normal or single (single is a SW stop) | c:443-461 |
| 0x1F | W | 16-bit | read start address, derived from 0x14, sent before each ADC stream | c:598,647 |
| 0x20 | R stream | N bytes | CH1 ADC1 samples | variables.h:323 |
| 0x21 | R stream | N bytes | CH1 ADC2 samples | variables.h:324 |
| 0x22 | R stream | N bytes | CH2 ADC1 samples | |
| 0x23 | R stream | N bytes | CH2 ADC2 samples | |
| 0x28 | W | 1 byte (0) | short time base mode select | c:494-496 |
| 0x32 | W | 16-bit | CH1 offset DAC, default 750, calibration probes 500..1200 | c:290-298 |
| 0x33 | W | 0..5 | CH1 V/div hardware step (clamped to 5) | c:273-288 |
| 0x34 | W | 1 byte | CH1 coupling, 1 = DC, 0 = AC | c:253-271 |
| 0x35 | W | 16-bit | CH2 offset DAC | variables.h:318-324 |
| 0x36 | W | 0..5 | CH2 V/div | |
| 0x37 | W | 1 byte | CH2 coupling | |
| 0x38 | W | 16-bit | LCD backlight PWM, 0..0xEA60 | c:176 |
| 0x3C | W | 16-bit, constant 32431 | "battery level?" unexplained, written once at start | c:890-899 |
Per-channel command assignment: `scope_load_configuration_data` (scope_functions.c:7827-7870).

Capture sequence (`fpga_do_conversion`, c:478-539; `scope_acquire_trace_data`, scope_functions.c:4232-4334):
1. Rewrite trigger level (0x17) and timebase (0x0E), `samplemode = 1`.
2. 0x0F <- 0 (trigger on) or 1 (calibration, auto set); 0x28 <- 0; 0x01 <- 1 (reset).
3. cmd 0x05, poll bit0 until 1 (twice); 0x01 <- 0.
4. cmd 0x0A, poll bit0 until 1. In triggered mode this loop also runs `tp_i2c_read_status()` and exits on touch, so the UI stays live in NORMAL/SINGLE with no trigger. Then 0x0F <- 1.
5. cmd 0x14, read 16 bit, `& 0x0FFF`, plus 2. Software then maps to a start address: `if(v < 750) v += 3345; else v -= 750` (scope_functions.c:4264-4275). Since 3345 + 750 = 4095, the capture RAM is a ring of about 4095 entries per ADC and reading starts 750 samples before the trigger, centring the trigger in a 1500-sample window. INFERRED.
6. Per enabled channel: write 0x1F <- start, cmd ADC1, read 1500 bytes; write 0x1F <- start again, cmd ADC2, read 1500 bytes (`fpga_read_sample_data`, c:587-703).
Sample layout: each channel `tracebuffer` is 3000 bytes. ADC1 fills odd indexes (1,3,5...), ADC2 fills even (0,2,4...): two interleaved ADCs (c:608-612, 654-660, `buffer += 2` at c:870-874). Reads per capture are fixed at `SAMPLES_PER_ADC` = 1500 per ADC, `MAX_SAMPLE_BUFFER_SIZE` 3000 (variables.h:198-202), not the full FPGA RAM.
Samples are unsigned 8-bit, 128 = centre. Per-ADC mismatch is corrected in SW with `adc1compensation`/`adc2compensation` (int16) including a special case near the rails (c:716-770).
Trigger: source, edge and level are hardware (FPGA). The on-screen alignment is software: `scope_process_trigger` scans 20 samples at index 1490..1509 for a level crossing (scope_functions.c:4336-4388). Comment at :4318 admits it flips between two positions at low rates.

Tables (variables.c):
- `sample_rate_settings[18]` (cmd 0x0D data, :364-385): 0 (200 MSa/s), 1 (100), 3 (50), 9 (20), 19 (10), 39 (5), 99 (2), 199 (1 MSa/s), 399 (500 k), 999 (200 k), 1999 (100 k), 3999 (50 k), 9999 (20 k), 19999 (10 k), 39999 (5 k), 99999 (2 k), 199999 (1 k), 399999 (500 Sa/s). The data is divider minus 1 of a 200 MHz clock.
- `timebase_settings[24]` (cmd 0x0E data, :447-474): 200/100/50/20/10 ms/div = 1800; 5 ms, 2 ms, 1 ms, 500 us, 200 us = 2500; 100 us = 3000; 50 us = 5596; 20 us = 9692; 10 us and 5 us = 21980; 2 us = 83420; 1 us = 206300; 500 ns down to 5 ns = 411100. The 20 ms entry has a "was 800, not enough samples" comment.
- Time/div index 0..23 = 200 ms ... 5 ns (variables.c:292-298). `time_per_div_sample_rate[24]` (:234) maps time/div to sample-rate index: 200/100/50 ms/div all use 2 kSa/s, 500 ns/div and faster all use 200 MSa/s (the fast ranges just zoom the same 200 MSa/s data). `viable_time_per_div[18][24]` (:268) marks combinations that fill a screen.
- Display mapping: `disp_xpos_per_sample = 50 * freq_per_div / sample_rate` pixels per sample (scope_functions.c:5084).

### 1.6 Analog front end
- V/div: hardware steps 0..5 (relay/attenuator network) plus step 6 = software 2x zoom of step 5. Scale: `signal_adjusters[7]` = {3629021, 3670975, 3775860, 3775860, 3859768, 3859768, 7719536}, right shift `VOLTAGE_SHIFTER` 21 (variables.c:362, variables.h:138) giving pixels per ADC count. Labels (x1 probe): 5V, 2.5V, 1V, 500mV, 200mV, 100mV, 50mV per div; x10 and x100 rows follow (variables.c:349-354).
- 50 ms wait after V/div changes for relays (scope_functions.c:4440, statemachine.c:2937).
- AC/DC: cmd 0x34/0x37, 1 = DC. Probe magnification is SW labelling/scaling only.
- Offset DAC: cmd 0x32/0x35, 16-bit, `dc_calibration_offset[7]` per V/div per channel (variables.h:272). Note at scope_functions.c:4426: lower value gives higher ADC reading.
- No bandwidth limit, external trigger or AWG code.

### 1.7 Battery and power
- Battery: KEY ADC cfg 0x010000159, data `& 0x3F`, brightness compensation, minus 7 when charging, minus 25 floor, times 20/21 (power_and_battery.c:46-99). Drawn by `scope_battery_status` (scope_functions.c:1959). Runs every main-loop pass.
- Power button on PE11 EINT (negative edge, INFERRED from cfg 1). Handler saves settings to SD sector 700 then spins forever (power_and_battery.c:121-138). The time window before power drops is UNKNOWN.

### 1.8 SPI NOR flash
- SPI0 on PC0-3, clock AHB/4 (`SPI_CCR_DRS_DIV_2 | CDR2(2)`, spi_control.c:25-32). Comments: some chips fail at the faster setting, zero is too fast. Commands: 0x03 read, 0x05 status, 0x06 WREN, 0x20 4 KiB sector erase, 0x02 256 B page program (spi_control.c:46-205).
- The flash holds the original FNIRSI firmware and original settings at 0x1FD000; the new firmware has those paths disabled (`#if 0`, scope_functions.c:7816-7822, :7879-7881) and keeps settings on SD. `SS/flashchips.h` and `SS/todo.txt` describe a flash backup tool.

### 1.9 SD card (SDC0) and filesystem
- `sd_card_interface.c` is a bare-metal SD host driver: init at 400 kHz 1-bit, then 48 MHz 4-bit if supported (`sd_card_set_clock_and_bus`, :538; PLL_PERIPH 576 MHz source, :774-781). PIO through the FIFO, no DMA (comment :932-935 says the original uses DMA for large transfers). CMD12 is never issued, auto-stop is used (:919). 4 KiB staging buffer `sd_buffer[1024]` (uint32). Handles SDSC, SDHC, MMC detection.
- FatFs with LFN (`FF_USE_LFN` 1, static buffer, code page 932, no exFAT, 512 B sectors; ffconf.h:11,87,116,197,234). `diskio.c` wraps `sd_card_read/write`; constant timestamp 1349957149. TLSF `malloc.c` is present but its pool is never created (dead code).
- Author notes (SS/todo.txt): a 4 GB card that works in a card reader failed in the scope.

---------------------------------------------------------------------------------------------------
## 2. Firmware architecture

### 2.1 Boot chain
Two chains (README.md:14-27):
- A (with startup screen): BROM -> `BL` -> `SS` -> `S`.
- B (without): BROM -> `NS` (also brings up the FPGA) -> `S`. Faster but not updated for the display-config sector (README.md:26-27).
Details:
- BL and NS carry a BROM `eGON.BT0` header with "SPL",2 (BL/start.s:1-12), linked at 0x0 in a 32 KiB region (BL/fnirsi_1013d.ld). BL is 14848 B (comment BL .c:12: under 16 KiB).
- SS and the scope carry an `eGON.EXE` header (S/start.s:4-9). That is not a BROM image. The previous stage reads the 512 B header sector, checks "eGON.EXE" at offset 4, takes the length from bytes 16..19, copies from offset 32 to the load address and jumps (NS .c:65-98, SS .c:104-148). The scope is linked at 0x7FFFFFE0 so code starts at 0x80000000. Verified from the binary: header size field 0x38E00 = 232960 B.
- BL (`BL/fnirsi_1013d_sd_card_bootloader.c`): clock init, DRAM init (`BL/dram_control.c`), caches, SD init, reads sector 48, loads SS at 0x81C00000 (4 MiB window), also reads sector 710 to 0x81BFFC00, jumps (:18-95).
- SS (`SS/fnirsi_1013d_startup_screen.c`): linked at 0x81BFFFE0 (SS/fnirsi_1013d.ld:2). Inits display, draws "PECOs sCOPE", inits FPGA (`fpga_check_ready` waits for version 0x1432), backlight 0xEA60, loads the scope from SD sector 92 to 0x80000000, waits at least 750 ms, jumps (:25,78-148). Failure colours: red SD init, yellow sector read, magenta bad header, cyan body read.
- NS: same without a screen, loads sector 48 directly to 0x80000000 (NS .c:13,79-96).
- SD layout (512 B sectors): `dd bs=1024 seek=8` puts the packed image at byte 8192 (sector 16). Inside the image BL is at +0 (sector 16), SS at +0x4000 (sector 48, `-l 0x4000` in SS/Makefile), scope at +0x9800 (sector 92, `-l 0x9800` in S/Makefile). Settings sector 700 (variables.h:28). Display/touch config sector 710 (`dd seek=355`, configuration_file.txt). Scope image ends near sector 547. FAT32 partition must leave 1 MiB free at the start (README.md:36).
- Why it boots from SD: INFERRED that the BROM tries SD0 boot0 at 8 KiB before the SPI flash; the removal recipe zeroes 1 KiB at that offset to restore the original boot (README.md:48-52).
- FEL: `sunxi-fel -p write 0x7FFFFFE0 fnirsi_1013d_scope.bin exe 0x80000000` (How_to_load_scope.txt). `main()` never calls `sys_dram_init`, so DRAM must already be initialised. How the board enters FEL and gets DRAM is not documented.
- Backup scheme for the original firmware (SS/todo.txt): dump SPI NOR to SD. Note there: first byte of the full flash read comes back 0x00 instead of 0x06.

### 2.2 Memory map, linker script
- `S/fnirsi_1013d.ld`: one region `ram: org 0x7FFFFFE0, len 32M`; `.text` (start.o first), `.data` (with `.rodata`, all in RAM, no copy), `.bss`; symbols `PRGM_START/END`, `BSS_START/END`, `__program_size`. `main()` zeroes BSS itself (fnirsi_1013d_scope.c:39). No heap.
- Large static buffers (variables.c): `maindisplaybuffer` 768000 B (:57), `displaybuffer1/2` 768000 B each (:59-60), `viewthumbnaildata[1000]` (~406 B each, 400000 B; also the SCSI bounce buffer), `channel1tracebuffer` and `channel2tracebuffer` 3000 B each (:81,85), point buffers 730 entries each.
- Fixed DRAM addresses: display cfg 0x81BFFC00, touch cfg 0x81BFFC1C, SS 0x81BFFFE0, chain-load window 0x81C00000, stacks 0x81FB5000-0x82000000.

### 2.3 Main loop and state machine
- `fnirsi_1013d_scope.c:36-178` `main()`: BSS clear, clocks, caches, timer0, power IRQ, battery, enable IRQs, SPI flash init, `fpga_init`, backlight 0, `sys_init_display`, `tp_i2c_setup`, `f_mount` (hang with red "SD ERROR" on failure, :82-99), `usb_device_init` (device disabled until the USB screen), `scope_load_configuration_data`, push settings to FPGA, `scope_setup_main_screen`, first `scope_display_trace_data`, backlight, then:
  ```
  while(1){ battery_check_status(); scope_acquire_trace_data(); scope_display_trace_data(); touch_handler(); }
  ```
  (:164-177). Single thread, one capture, one full redraw and one touch scan per pass. Acquisition blocks in `fpga_do_conversion`.
- UI: `statemachine.c` `touch_handler()` (:25) -> `scan_for_touch()` (:174) -> `handle_*_menu_touch`; drag states `TOUCH_STATE_*` (fnirsi_1013d_scope.h:14-27). `timer0_delay(50)` throttles drags (statemachine.c:109). Run/stop is `scopesettings.runstate`. Trigger mode 0 auto / 1 normal / 2 single through `set_trigger_mode` (statemachine.c:2946, called at :1275-1287); single stops after one capture (scope_functions.c:4251-4259).
- Settings structs `SCOPESETTINGS`/`CHANNELSETTINGS` (variables.h:232-390) hold everything a remote API would set (sample rate index, time/div, trigger fields, per-channel enable/coupling/V/div/offset/measurements).

### 2.4 Acquisition and processing pipeline
1. `scope_acquire_trace_data()` (scope_functions.c:4232) runs only if `runstate == 0 && touchstate == 0`. Capture as in 1.5.
2. `fpga_read_sample_data()` (fpga_control.c:587) reads ADC1 then ADC2 per enabled channel and, inside the byte loop (c:705-885), computes min, max, sum, sum of squares (RMS through `isqrt`), plus a hysteresis zero-crossing state machine at centre +/- 10% of Vpp (c:620-660). Derived: centre, peak-peak, frequency (needs > 2 crossings), high/low/period time in 2^-20 sample units. The 12 measurements per channel: Vmax, Vmin, Vavg, Vrms, Vpp, Vp, Freq, Cycle, Time+, Time-, Duty+, Duty- (scope_functions.c:5568-5800). All CPU work, no hardware assist.
3. Always-50% trigger recentres the trigger level after each capture (scope_functions.c:4283-4313).
4. `scope_process_trigger` (:4336): software alignment.
5. `scope_display_trace_data()` (:5049) clears the trace area of `displaybuffer1`, draws grid, then `scope_display_channel_trace` (:5315) maps samples to pixels with `double` maths (software float on this core) and draws lines (linear interpolation only; sinc idea in comment :5054). Then cursors, pointers, measurements, then `display_copy_rect_to_screen(2,46,728,434)` memcpy into `maindisplaybuffer` (about 632 KB per frame). XY mode draws lines between CH1 (x) and CH2 (y) samples (:5143-5165). FFT is not implemented (comment :5136, `fftenable` flag only).
6. Auto setup (`scope_do_auto_setup`, :4655): most sensitive HW range, probes 200 MSa/s, 2 MSa/s, 20 kSa/s, 1 kSa/s until a frequency is found, picks time/div for 2.5-3 periods, then tunes V/div (steps 3, 1, 0) via `scope_check_channel_range` (:4991).
7. Calibration (`scope_do_baseline_calibration` :4390, `scope_do_channel_calibration` :4486): inputs must be open. For each HW V/div step, at two sample rates (200 MSa/s and 20 kSa/s), it measures ADC1 mean at offset DAC 500 and 1200, derives DAC counts per ADC code (734003200 / delta, fixed point 2^20), solves for the DAC value giving code 128, averages the two. Then verifies at 2 MSa/s (ADC1 raw mean must be 125..131, ADC difference within +/-20), computes `adc1compensation`/`adc2compensation` and trims the DAC. Results go to the settings sector.

### 2.5 Display library (`display_lib.c/.h`)
- Immediate-mode software renderer into RGB565 buffers selected by `display_set_screen_buffer`. Colours given as 0x00RRGGBB and reduced to 565 (display_lib.c:57-66). Primitives: line, horz/vert lines/dots/dashes, rect, rounded rect, arcs (sin table in `sin_cos_math.c`), fills, y-gradient, 1-bpp icon blits (`icons.c`), fixed- and variable-width fonts (`font_*.c`, fonts 0,2,3,4,5 linked), slide-in animations, copy rect to/from screen (per-line memcpy, ARM asm `memcpy.s`).
- Double buffering is manual: build in `displaybuffer1`, memcpy a rect into `maindisplaybuffer`. No DEBE page flipping or vsync; tearing possible.

### 2.6 Settings and display-config sectors
- Sector 700 = `settingsworkbuffer[256]` of uint16 (512 B): words 0..1 checksum, 2..3 version 0x0100, 0x0002 (variables.h:98-99). Offsets: CHANNEL1 10, CHANNEL2 40, TRIGGER 70, OTHER 100, CURSOR 130, MEASUREMENT 160, CALIBRATION 200, ADC compensation at 220 (variables.h:90-96; save `scope_functions.c:7964-8082`). Load checks checksum and version, else resets defaults and rewrites (:8084-8212).
- Defaults (scope_functions.c:7890-7962): CH1/CH2 on, DC, 1x, V/div idx 0, trace positions 300/100, auto trigger, rising, CH1, trigger position 362/200, 20 us/div, 5 MSa/s, brightness 80, grid 25, always-50% on, offset DAC 750.
- Display/touch config sector (configuration_file.txt): preamble AAAAAAAA 55555555, TCON timing1, timing2, postamble CCCCCCCC 33333333, sum of the six words, then two bytes xswap, yswap (0x00 swap, 0xFF none) not covered by the checksum. Write with `dd ... bs=1024 seek=355`.

### 2.7 Waveform and picture files
- `\waveforms\<n>.wav`, `\pictures\<n>.bmp`, thumbnails in `\waveforms\wav_thumbnails.sys` and `\pictures\bmp_thumbnails.sys` (variables.c:607-624, names via `scope_print_file_name` :6243).
- Waveform layout (`waveform_file_format.txt`, `scope_prepare_setup_for_file` :5976): 800 B settings block (200 little-endian uint32), then 3000 B CH1 samples, then 3000 B CH2 samples (u8, ADC1/ADC2 interleaved as captured). Checksum at offset 0 = sum of uint32 words 1..199 plus all sample bytes as 750 uint32 words per channel. Offset 4 = version 0x01000002 (variables.h:101).
- Field offsets: CH1 block at 40, CH2 block at 160 (enable, display V/div, sample V/div, FFT, AC/DC, probe, trace position, min, max, avg, centre, Vpp, freq valid, freq, low time, high time, period), trigger block at 280 (time/div, sample rate, mode, edge, channel, level, h-pos, v-pos, have-trigger, trigger index), other at 400, cursors at 520, measurement enables at 640 (12 per channel). Full table: waveform_file_format.txt:7-104. RMS is not stored.
- BMP screenshots: 800x480 RGB565 with a 70-byte header (`PICTURE_HEADER_SIZE`, variables.h:109-111), file size 70 + 768000.
- Useful as a ready-made capture description for a PC tool (settings plus raw samples).

### 2.8 USB (device mode, mass storage only)
- Stack: hand-written bare-metal Mentor/MUSB-style OTG device driver for the F1C100s. No TinyUSB or vendor stack. Files: `usb_interface.c/.h` (826/361 lines), `mass_storage_class.c/.h` (603/109). Registers at 0x01C13000 (FIFO per endpoint at +4*ep, EPIND 0x42, CSR0/TXCSR 0x82, RXCSR 0x86; PHY via ISCR 0x01C13400 and CSR 0x01C13410; usb_interface.h:12-76).
- Init (usb_interface.c:75-104): ungate PHY and OTG in CCU, three PHY tuning writes via `usb_phy_setup`, FIFO config, force ID and VBUS valid with pull-ups, PIO FIFO mode (no DMA), IRQ 26 priority 2. Not connected until `usb_device_enable()` sets SOFT_CONNECT (:106-123) after enabling high speed and reset/suspend/resume/disconnect interrupts.
- Descriptors (mass_storage_class.c:14-110): VID 0x0483, PID 0x5720 (STM sample IDs), bcdUSB 2.00, EP0 64 B, one configuration, one interface (class 8, subclass 6, protocol 0x50 BOT), EP1 IN 0x81 and EP1 OUT 0x01, bulk, 512 B. Strings are copy-pasted: manufacturer "HeroJe Scanners Drives - H2750", product "H2750  Usb Devices", serial and interface "CDC ACM config". SCSI INQUIRY vendor/product "F1C100S  XiaoTaoQi  Disk 1.0" (:112-119).
- IRQ handler `usb_device_irq_handler` (usb_interface.c:352-826): RESET/RESUME/SUSPEND/DISCONNECT, EP0 state machine (IDLE, IN_DATA_PHASE) for GET_DESCRIPTOR (device, config, strings), SET_ADDRESS (deferred), SET_CONFIGURATION (configure EP1 RX and TX: 512 B, double-buffered, RX FIFO at 0, TX FIFO address 128, enable EP1 IRQs; :583-640), CLEAR_FEATURE, class request 0xFE GET_MAX_LUN (returns 0). Not handled: GET_STATUS, GET_CONFIGURATION, SET_INTERFACE, SET_FEATURE, device qualifier, other-speed config, mass-storage reset 0xFF (commented out). High speed only in practice (comments :459-466 and :583-600).
- Mass storage BOT/SCSI (`mass_storage_class.c`): states WAIT_COMMAND, SEND_DATA, RECEIVE_DATA, SEND_STATUS. Commands: INQUIRY, TEST UNIT READY, START STOP UNIT, PREVENT ALLOW MEDIUM REMOVAL, READ CAPACITY(10), MODE SENSE(6) (answers with the inquiry string), READ(10), WRITE(10). Everything else returns FAIL. No REQUEST SENSE. Backing store is the raw SD card, with `viewthumbnaildata` (400000 B) as the bounce buffer (`SCSI_MAX_BLOCK_COUNT` 781 blocks).
- Mode switching: USB is a modal screen. Main menu "USB connection" calls `scope_setup_usb_screen()` (scope_functions.c:184-255; call at statemachine.c:792), which draws a PC cartoon, calls `usb_device_enable()`, then blocks in a touch-polling loop until the user taps ON/OFF, then `usb_device_disable()` and `scope_sync_thumbnail_files()`. No acquisition, display refresh or measurement runs while USB is active. The DISCONNECT interrupt only clears flags; comment says code to exit USB mode on cable removal is missing (usb_interface.c:421-424).

### 2.9 Calibration (summary)
Per-channel, per-V/div offset DAC plus per-ADC additive compensation, stored in the settings sector. No gain, timebase or probe-compensation calibration. SS/todo.txt records a wish for FPGA support to calibrate the two ADCs to match.

---------------------------------------------------------------------------------------------------
## 3. Toolchain, build, flash, debug
- Compiler `arm-none-eabi-gcc`. Flags (S/nbproject/Makefile-Debug.mk): `-Wall -Wno-write-strings -Wno-char-subscripts -fno-stack-protector -DNO_STDLIB=1 -mcpu='arm926ej-s' -O3 -mfloat-abi=soft`. BL uses `-O0`; SS has `-O3` without the float flag. Link: `arm-none-eabi-gcc -T./fnirsi_1013d.ld -nostdlib -lgcc` (Makefile-Debug.mk:93). No libc; hand-written ARM asm `memcpy/memmove/memset/memcmp/strcpy/strlen`.
- Build system: NetBeans projects (`nbproject/`, generated `Makefile-Debug.mk`) plus a top-level `Makefile` whose post-build step runs `objcopy -O binary`, `./mksunxi <bin>`, then `./flashfilepacker -o dist/.../fnirsi_1013d.bin -i dist/.../fnirsi_1013d_scope.bin -l 0x9800` (S/Makefile). CLI build is `make CONF=Debug` per project, on Linux or WSL (the two helper ELFs are 64-bit Linux). BL's post-build copies its bin into SS's dist folder, and SS's post-build copies its packed image into S's dist folder, so build order is BL, SS, S for the full image.
- `compile.sh`, `burn.sh`, `debug.sh` in each project are stale STM32F103 leftovers (cortex-m3, openocd STM32F103C8T6.cfg). Not usable here.
- Flashing: (1) SD card with FAT32 starting at 1 MiB, `dd if=fnirsi_1013d.bin of=/dev/sdX bs=1024 seek=8`, then the display/touch config sector at `seek=355` (README.md:30-47, How_to_load_scope.txt). (2) FEL via `sunxi-fel` (2.1). Removal: zero 1 KiB at seek=8. Prebuilt Windows image `V0.005_Windows.7z` also exists.
- Debugging: nothing built in. No UART output, no semihosting, no JTAG config, no printf. PC3 (UART0 TX alt) is shared with SPI0 MOSI. The author used FEL for quick iteration, on-screen text, colour-coded error screens, USB captures of the original firmware in Wireshark (SS/todo.txt) and Ghidra on the original code (mass_storage_class.c comments).

---------------------------------------------------------------------------------------------------
## 4. Known bugs, limitations, TODOs, performance
Explicit in code and docs:
- Trigger alignment in SW flips between two positions at low rates (scope_functions.c:4318); needs a wider check (:4349).
- Trigger position at 200-20 ms/div: "Not sure if they work or need to be done in software" (:4105).
- Display: "last pixel interleaving is not working properly. Don't know why" (:5052); sinc interpolation not implemented (:5054); idea to redraw only on change (:5058-5063); FFT not implemented.
- `fpga_do_conversion` semantics of 0x0F and 0x05 marked with question marks (fpga_control.c:483,508,528); 0x04 and 0x3C unexplained.
- Frequency needs more than 2 crossings, otherwise "xxxHz" placeholders (scope_functions.c:5700-5748).
- Touch: single point only; minimum delay constraint (touchpanel.c:530).
- SD: some cards fail (SS/todo.txt); MMC path and card type check of unclear need (sd_card_interface.c:169).
- USB: high speed only, modal, no recovery on cable pull, unverified CLEAR_FEATURE (usb_interface.c:421, :673).
- Power loss: settings saved in the IRQ then hang; no wear levelling (power_and_battery.c:127).
- `NS` variant not tested with latest code (README.md:26-27).
- Fixed 3000 samples per channel, no deep memory, no roll mode: long time bases were removed on 2021-11-22 (fpga_control.c:303-306).

Performance numbers: none are documented in the repo (no FPS, no captures per second, no throughput). Only derivable facts:
- Per capture 2 channels x 2 ADCs x 1500 = 6000 byte reads, each costing two read-modify-write strobe accesses plus one data read on the GPIO block (about 5 APB register accesses per byte). Actual rate UNKNOWN, needs measurement.
- Per frame cost: soft-float `double` mapping, software line drawing, 728x434 RGB565 memcpy, plus a touch I2C status read and battery read per loop. Likely the dominant cost.
- Nominal rates: 18 steps from 200 MSa/s to 500 Sa/s, claimed hardware bandwidth 100 MHz.
Likely improvement levers (my assessment): enable MMU with cacheable DRAM, replace `double` by fixed point in trace mapping, skip redraw when stopped or unchanged, speed up the FPGA read loop (precomputed PE_DATA values, unrolled, no read-modify-write), use the touch INT pin instead of polling, DMA for SD and display copies, use the FPGA RAM beyond 1500 samples if the 0x1F address allows.

---------------------------------------------------------------------------------------------------
## 5. Adding a PC command interface (USB CDC or similar)
Existing structure:
- All USB logic is in `usb_interface.c` and `mass_storage_class.c`. EP1 (bulk IN and OUT, 512 B, double-buffered) is wired directly to the SCSI state machine from `usb_device_irq_handler` (usb_interface.c:743-826). Descriptors are single-purpose constants served from the EP0 GET_DESCRIPTOR switch (usb_interface.c:~500-560). The config struct `Mass_Storage_Descriptor` hard-codes 1 interface and 2 endpoints (usb_interface.h:325-330).
- The F1C100s MUSB has EP1-EP4; the header defines interrupt flags up to EP5 and FIFO registers EPFIFO1..5 (usb_interface.h:40-47,181-194). Only EP1 is configured. FIFO RAM is split RX at 0 and TX at 128 (units of 8 B), so more endpoints fit if FIFO addresses are chosen above 256 (total FIFO RAM size should be checked in the datasheet).
- Reusable: `usb_write_to_fifo`, `usb_read_from_fifo` (alignment aware), `usb_write_ep1_data`, EP0 enumeration, PHY setup, IRQ plumbing.
Difficulty assessment:
1. New device class: moderate. Write new device/config descriptors, extend the EP0 handler (class requests other than 0xFE are currently ignored, and the status stage is skipped, usb_interface.c:~655-680), configure extra endpoints in SET_CONFIGURATION, route OUT data into a parser. Easiest is a vendor-specific bulk interface (libusb/WinUSB with WCID descriptors). CDC-ACM needs an IAD, a notification interrupt endpoint and SET_LINE_CODING/GET_LINE_CODING/SET_CONTROL_LINE_STATE handling, but works with stock host drivers. A composite MSC + CDC device is possible but risky because host and scope would share the FAT volume.
2. Concurrency is the hard part. Today USB runs only inside a blocking screen. For a control interface the USB device must be enabled at boot, and a `command_poll()` must be called from the main loop (fnirsi_1013d_scope.c:164). `fpga_do_conversion` can wait forever for a trigger and only aborts on touch, so the poll hook or an ISR-set abort flag must go into those wait loops (fpga_control.c:518-535). The 50 ms drag throttle and long screen redraws also add latency.
3. Data path is small: a capture is 6000 B plus metadata. A host fetch is a copy of `channel1tracebuffer`/`channel2tracebuffer` (3000 B each, ADC1/ADC2 interleaved, odd/even) plus `scopesettings` (sample rate index, time/div, V/div index, trace position), `disp_trigger_index`, `disp_have_trigger` and the compensation values. Volts: `(sample - 128) * signal_adjusters[samplevoltperdiv] >> 21` gives screen pixels; convert with the V/div tables. Time axis from `sample_rate[samplerate]`.
4. Remote setup uses existing setters in this order: edit `scopesettings.*`, then `fpga_set_channel_enable/coupling/voltperdiv/offset`, `fpga_set_sample_rate`, `fpga_set_time_base`, `fpga_set_trigger_channel/edge/level/mode`, then `runstate`. `fpga_set_trigger_level` derives the level from screen positions (fpga_control.c:397-441), so a PC API should write cmd 0x17 directly with a 0-255 value.
5. Transport capacity: PIO FIFO with 512 B high-speed bulk packets is ample for 6 KB captures. No DMA, so no cache coherency worry (and the D-cache is probably inactive anyway).
6. Rough effort: descriptors, class requests and endpoint setup 300-500 lines C; command parser and glue 300 lines; main-loop and trigger-wait integration is the delicate part.
7. Signal generator: not present in firmware or (as far as this code shows) in hardware. Nothing to build on.
8. No alternative host channel exists today (no UART, no network). SD-card file exchange through the MSC mode is the only one.

---------------------------------------------------------------------------------------------------
## 6. Read these first
1. `README.md`, `S/dist/Debug/GNU_ARM-Linux/How_to_load_scope.txt` for build and flash flow.
2. `S/fnirsi_1013d_scope.c` (init order, main loop), `S/start.s`, `S/fnirsi_1013d.ld`.
3. `S/fpga_control.h`, `S/fpga_control.c` (entire FPGA protocol), `S/variables.c:197-560` (rate, timebase, scaling tables), `S/variables.h:190-400` (limits, structs).
4. `S/scope_functions.c`: `scope_acquire_trace_data` (:4232), `scope_process_trigger` (:4336), calibration (:4390-4633), `scope_do_auto_setup` (:4655), `scope_display_trace_data` (:5049), `scope_display_channel_trace` (:5315), `scope_prepare_setup_for_file` (:5976), config load/save (:7814-8212).
5. `S/usb_interface.c/.h`, `S/mass_storage_class.c/.h` for everything a new USB interface would extend.
6. `S/statemachine.c` (:25 `touch_handler`, :174 `scan_for_touch`, :2946 `set_trigger_mode`).
7. `S/display_control.c`, `S/touchpanel.c`, `S/power_and_battery.c`, `S/ccu_control.c`, `S/sd_card_interface.c`, `S/spi_control.c` for peripheral bring-up.
8. `BL/fnirsi_1013d_sd_card_bootloader.c`, `BL/dram_control.c`, `SS/fnirsi_1013d_startup_screen.c` for boot chain, DRAM parameters, sector layout.
9. `waveform_file_format.txt`, `configuration_file.txt`, `SS/todo.txt` (author's raw notes on SD layout, flash backup, ADC calibration).
External: pecostm32/FNIRSI-1013D-Hack (packer, history), Atlan4/Fnirsi1013D (later fork), EEVblog thread https://www.eevblog.com/forum/testgear/fnirsi-1013d-100mhz-tablet-oscilloscope/.

## 7. Open questions for later phases
- FPGA internals (bitstream not in repo): ring depth, meaning of 0x0E, 0x28, 0x3C, 0x04, whether more than 1500 bytes per ADC can be streamed, maximum strobe rate.
- Measured capture rate and frame rate (needs a free-running counter; timer0 only has 1 ms resolution).
- How FEL is entered and DRAM initialised on this board.
- Real AHB and APB clocks (300/150 MHz per comment) and the GPIO access cost.
- Whether the D-cache has any effect without the MMU.
