# Atlan4/Fnirsi1013D repo: technical digest

Repo: `D:\src\Fnirsi1013D` (github Atlan4/Fnirsi1013D, one git commit `b0397d6`). Continuation of pecostm32's open firmware, "PECO" alt firmware running from SD.
Extracted archives: `D:\src\fnirsi-1013d-ai\research\atlan4-extracted\<zip>\<zip>\...` (90 folders), 1.00x firmware in `...\atlan4-extracted\_dso1xxx\<name>\...`.
Companion notes (same folder):
- `atlan4-history-and-docs.md`: full timeline (16 phases, 207-row diff table), manual summary (antiword), readmes, waveform-file format, TODOs.
- `fpga-verilog-analysis.md`: FPGA Verilog (AL3/EF2/EP4), command map, resources, limits.
Paths below: `S28` = `D:\src\Fnirsi1013D\Scope v0.28a\`, `S100` = `...\atlan4-extracted\_dso1xxx\v1.00o5\fnirsi_1013d_scope\` (latest, 26-02-13), `S27R` = `...\v0.27r YES work it with APP\v0.27r YES work it with APP\fnirsi_1013d_scope\`, `S27T` = `...\v0.27t clear  usb interface code\v0.27t clear  usb interface kode\fnirsi_1013d_scope\`, `S26Y5` = `...\0.26y5 work ch340 ok\0.26y5 work ch340 ok\fnirsi_1013d_scope\`.

## 0. Bottom line for a PC/MCP interface

- There is NO usable PC protocol in the latest code (S100). `PC_interface.c` only holds a `sysparam` struct and an `#ECHO` stub. USB CDC in S100 is a debug console (letters a..k plus a number).
- The only real PC protocol is a text-line protocol in v0.27m..v0.27t (`#VER`, `#INFO`, `#READ`, `#WAV2,3`, ...), emulating the OEM FNIRSI PC app. Sample data goes out as ASCII hex, ~20 KB/frame, from the last acquired (3000-point) trace buffer. See section 2.
- The CDC device is ST VCP (VID 0x0483 PID 0x5740), High-Speed bulk 512 B, 2 interfaces. USB CDC mode is a persisted setting (`USB_CH340`) that excludes mass storage.
- Everything needed for a clean binary protocol exists: FPGA command set (section 3 and `fpga-verilog-analysis.md`), setters in `fpga_control.c`, ring buffers, and an IRQ handler. Main loop hook point: main loop in `S100 fnirsi_1013d_scope.c:~230-380`.

## 1. Version history (condensed; details in `atlan4-history-and-docs.md`)

Dating: no changelog. Dates from `build_time.h` (exists from v0.26y5) and `VERSION_STRING` comments in `variables.h:19`.

| Phase | Versions | What |
|---|---|---|
| Base | pecostm32 v0.002-0.006 (2022-23) | new open firmware; v0.004 TP X/Y swap in display config; v0.006 better RMS (ziutek) |
| RTC, UI | 0.012-0.019b | DS3231 RTC (0.014f first `DS3231.c`, on by default off); FatFs timestamps (0.015g); date/time in picture/wave view; auto-DC; cursor colors; sensitivity in channel menu |
| Menus, calibration | 0.019g-0.022 | `Menu.c` added at 0.020 (renamed `menu.c` 0.022k); input calibration; offset fixes |
| Autoset, long base | 0.022b-0.023x | autosetup rewrite; long (roll) time base 100 ms/div..50 s/div; settings moved to sector 709 (0.023d) |
| DC offset, avg | 0.024-0.024y | diag menu, DC-shift correction, FPGA average mode, A/V mode, probe ratios 0.5..1000; calibration moved to sector 708 (0.024q); long memory (0.024y, later "not working") |
| New bootloader | 0.025a-0.026a | boot menu PECO/FEL/FNIRSI (fw0.03/0.04, v0.025l); lock cursors; icon menus; kiss_fft + `ch340_class` + `commands.c/.h` added (0.025y1) |
| UI rework | 0.026b-0.026j | keyboard menu, invert button, rounded menu items |
| Ref/math | 0.26k-0.26v5 | trigger/channel/other menus; REF1..REF8 (0.26m->0.26u6), save/load/auto-load refs (0.26u9), math sum/diff (0.26q7), generator button (0.26q3), BMP time stamp (0.26r2), `ref_and_math.c` |
| Split | updater 0.01b-0.02o, bootloader fw0.02-0.04 | see section 4 |
| CH340 | 0.26y5 (25-10-31) | first CDC-ish serial (`ch340_class.c`), descriptors "H2750 Usb Devices", VID 0x0483 PID 0x5740 |
| CDC | 0.27i..0.27t (25-11-01..11-10) | 0.27i CDC works, MSC switched to CDC; 0.27m SET_LINE_CODING OK, `cdc_class.c` + `PC_interface.c` added, ch340 removed; 0.27p buffers 512; **0.27r "YES work it with APP"** (25-11-05); 0.27s callback moved to main loop; 0.27t cleanup (last version with PC protocol) |
| 0.28a | 25-11-19 | branch off 0.27t: `MAX_SAMPLE_BUFFER_SIZE 16382`, "sample to 8000, data trigger to 8192"; FPGA cmds 0x1B/0x1C; fixed divider; no generator |
| 1.00x | 1.00g (25-11-24) .. 1.00o5 (26-02-13) | needs the PECO FPGA bitstreams (AL3/EF2/EP4). 1.00g gen FPGA cmds; 1.00j keyboard entry; 1.00l3 `generator.c`; 1.00l5 fix 25/75 % trigger; 1.00o5 EF2 circular buffer 6144, FPGA type in diagnostics |

Notes:
- 0.28a and 1.00x are divergent branches. 0.28a writes pretrigger/total with cmds 0x1B/0x1C (`S28 fpga_control.c:~575-579` region). The latest AL3/EF2 Verilog uses 0x0B/0x0C, and 0x1B is `hold_on_divider`. So 0.28a firmware does not match the latest bitstreams.
- Zips that extracted empty: `v0.020e to v0.021c`, `v0.26v4`.
- Many `*.txt` in repo root and `latest firmware version/` are renamed binaries (`eGON.BT0` images).

## 2. USB CDC / PC protocol

### 2.1 Physical/USB layer (S100)
- Files: `cdc_class.c/.h` (descriptors, ring buffers, EP callbacks), `usb_interface.c` (Allwinner MUSB device driver, IRQ handlers, EP0 setup), `PC_interface.c/.h`.
- Mode switch: setting `USB_CH340` (0 = mass storage, 1 = serial). Persisted via settings sector 709 (`scope_functions.c:6809,6920,7120`). Toggle in Other-settings menu (`menu.c:7201`, `statemachine.c:1423-1430`). USB menu screen (`menu.c:~270-330`) forces `USB_CH340=0` while the "Off USB" screen runs MSC, and sets it to 1 after leaving (so serial is on after quitting USB-MSC when the setting is serial).
- `usb_device_init()` (`usb_interface.c:~100-160`): enables USB OTG clock/PHY, PHY tweaks (45 ohm, amplitude, threshold), FIFO base, ISCR pull-ups plus forced ID/VBUS, PIO mode. If `USB_CH340==1`: 200 ms delay, `setup_interrupt(USB_IRQ_NUM, usb_device_CDC_irq_handler, 2)`, `usb_device_enable()`, 500 ms delay. Else MSC handler.
- Device descriptor (`cdc_class.c:45-61`): USB 2.0, class 0x02 (CDC), VID 0x0483, PID 0x5740, bcdDevice 0x0310, EP0 64 B. Strings: manufacturer "FNIRSI", product "FNIRSI 1013D", serial "12345678", config "FNIRSI Conf", ifaces "Custom CDC ACM" / "Data Interface".
- Config descriptor (`cdc_class.c:65-154`): 2 interfaces (CDC comm class ACM, then data class 0x0A), bus-powered (0xC0, 100 mA). Functional descriptors: header 0x0110, call mgmt, ACM, union. EP 0x81 interrupt IN (8 B, interval 10), EP 0x82 bulk IN and EP 0x02 bulk OUT, 512 B (`CDC_BULK_MAX_PACKET`). Host speed assumed High-Speed (`USBC_BP_POWER_D_HIGH_SPEED_EN`); no full-speed (64 B) fallback.
- FIFO setup (SET_CONFIGURATION, `usb_interface.c:~1100-1190`): EP1 TX FIFO 64 B (double-buffered) at 256; EP2 RX 512 B DPB at 0; EP2 TX 512 B DPB at 512.
- Class requests handled (`usb_interface.c:~1215-1260`, `~1760+`): SET_LINE_CODING 0x20 (7 bytes stored, default 9600 8N1, `port_line_coding`), GET_LINE_CODING 0x21, SET_CONTROL_LINE_STATE 0x22 (DTR or RTS set => `usb_connect=1`). `usb_connect` also set by RESET/RESUME, cleared by SUSPEND/DISCONNECT. Text/data TX is dropped unless `usb_connect` (`usb_CDC_send_text`, `usb_CDC_send_data`).
- Buffers (`cdc_class.c:8-14`): RX ring `usb_rx[1024]` and TX ring `usb_tx[32768]` with indices, but S100's RX path bypasses the ring: EP2 RX IRQ reads up to 64 B into `bufferCDC[64]` (`usb_interface.c:29`, ISR at `:1392-1420`; note `bufferCDC` is 64 B but the OUT packet can be 512 B, an overflow risk for host writes >64 B) and calls the parser directly in IRQ context.
- TX: `usb_CDC_send_text()`/`usb_CDC_send_data()` only append to `usb_tx` (`usb_interface.c:~1609-1650`). Nothing is sent until `usb_CDC_in_ep_callback()` (`cdc_class.c:245-296`) is called explicitly. It drains the ring in 512-byte packets, blocking on `TXCSR_D_TX_READY`. No ZLP handling, so a transfer that ends exactly on a 512 multiple needs the host to read with a timeout. `usb_tx_busy` is a plain flag.

### 2.2 Protocol in v0.27m..v0.27t (the "APP" protocol)
Source: `S27R usb_interface.c:~1450-1700` (v0.27r), `S27T usb_interface.c:1460-1675` (v0.27t, receive callback now called from main loop at `S27T fnirsi_1013d_scope.c:343`).

Framing: ASCII lines terminated by `\n` or `\r`. Line buffer `rx_buffer[128]`. Responses begin with `#OK,` or `#ERR,...`; some replies have no `#` prefix. All binary data is sent as two ASCII hex chars per byte (`send_hex_byte`, upper-case).

| Command (case: `#VER`, `#INFO`, `#RUN`, `#STOP`, `#READ` use case-insensitive compare; others `strncmp`) | Response | Notes |
|---|---|---|
| `#VER` | `V1.3.0C MOD V9B4\n` | version string copied from OEM FNIRSI firmware, PC app checks it |
| `#INFO` / `#MODEL` | `#OK,1013D\n` | `#MODEL` only in v0.27s |
| `#PING` | `#OK,PONG\n` | v0.27s only |
| `#ECHO,<text>` | `#OK,<text>\n` | |
| `#RUN` / `#STOP` | `#OK,RUN\n` / `#OK,STOP\n` | stubs, do not change state |
| `#SET,<anything>` | `#OK,SET\n` | stub |
| `#READ` | `#OK,` + 320 bytes of `SysParam` hex, comma separated, `\n` | |
| `#WAV2,2` | `#OK,<640 hex SysParam>,<9602 hex CH1>[,<9602 hex CH2>]\n` | test pattern ramps (no real data) |
| `#WAV2,3` | `#OK,<640 hex SysParam>,<9602 hex CH1>,<9602 hex CH2>\n` | real data; total ~19.85 kB ASCII. CH2 block only when `SysParam[2]==1` in v0.27r; always sent in v0.27t |
| anything else | `#ERR,UNKNOWN\n` | |
| any command while a `#WAV2,3` is in progress | `#BUSY\n` | via `usb_tx_busy` |
| first line after connect (v0.27m only) | `#OK,CONNECTED\n` | later commented out |

`#WAV2,3` payload: 4801 samples per channel. First 3000 come from `scopesettings.channelN.tracebuffer[0..2999]` (8-bit ADC values, 128 = center). In v0.27t each sample is multiplied by `input_calibration[samplevoltperdiv] >> VOLTAGE_SHIFTER`. The remaining 1801 samples are padded with 128. In v0.27r the CH2 block mistakenly used `channel1.tracebuffer`; v0.27t uses channel2. No time/level scaling is sent: the PC derives it from `SysParam`. The OEM app format expects 4801 points per channel, so the firmware stretches nothing, it just pads.

`SysParam` (`PC_interface.h:12-71`, packed, 320 bytes, offsets in comments are authoritative; initialised in `PC_interface.c:57-133`, a static snapshot NOT updated from live scope state): run_state[0] (0 stop, 1 run, 2 single wait), ch2_enabled[2], time_zoom[20] (2=5 ns .. 30=10 s, init 16), horiz_position[56..58] (3 B little endian, centre 1200000 = 0x124F80, range 50000..2350000), trigger_channel[64], trigger_mode[65] (auto/normal), trigger_edge[66], trigger_source[67], trigger_level[72] (0-255, 128 centre), ch1_volt_zoom[74] (4..13), ch1_vert_position[80..81] int16, ch1_coupling[116], ch1_probe[117] (1x,10x,100x), ch2_volt_zoom[140], ch2_vert_position[148..149], ch2_coupling[180], ch2_probe[181], xy_mode[249], signal_gen[250], selected_channel[284], trigger_edit[286], menu_flags[287]. Everything else reserved (zeros). This mirrors the OEM FNIRSI parameter block so the OEM Windows app can parse it (the app itself is not in the repo; the manual has no PC app section and credits "PC and Atlan").
Gap: the `SysParam` fields are not kept in sync with the real settings, so the PC sees constants.

### 2.3 What the latest code (v1.00o5) does instead
`S100 usb_interface.c:1461-1580` `usb_CDC_receive_callback()`:
- Empty line (just Enter): prints `#Aktualne hodnoty: ` and `a: ..` .. `k: ..` (global debug vars `a..k` defined `usb_interface.c:1471-1481`: a=310 b=320 c=70 d=50 e=1000 f=3000 g=0 h=70 j=182 k=20), then `#DONE`. (Bug: the `c:` and `i:` lines print loop-local `c`/`i`, not the globals.)
- `<letter a..k><decimal>`: sets variable; `f` sets `fpgasettings.totalsamples` (memory depth, no validation), `g` sets `scopesettings.ACQ_trace`, `e==1` triggers `scope_save_view_item_file(VIEW_TYPE_PICTURE)` (screenshot to SD). Reply `#value: <line>\n#END\n`.
- Else `#ERR\n#END\n`.
- `PC_interface.c:249-275` `usb_CDC_process_rx` (`#ECHO` only, body commented) is not called from the main loop in S100 (call commented out at `fnirsi_1013d_scope.c:288-295`).
- Commented leftovers: boot-time greeting `#OK,CONNECTED` + `V1.3.0C MOD V9B4` (`fnirsi_1013d_scope.c:~280-290`).
- Debug dumps of CCU/WDT registers over CDC in `menu.c:2156-2218`.
So: S100 retains `usb_CDC_send_text/send_data/send_hex_byte/usb_send_uint` and ring buffers, but the `#`-command parser has to be re-implemented on top of them.

### 2.4 Other serial/command code in repo
- Updater trees (`updater 0.0xx`) carry `ch340_class.c`, `usb_cdc.c` (682 lines, unused DWC-style stub that does not even build against this driver) and `commands.c/.h` (pecostm32-style token parser: `LINE_LENGTH 64`, `MAX_TOKENS 4`, variable/operator/number tokens, error codes). Not wired in the updater main (`fnirsi_1013d_updater.c`). This is a candidate base for a console-style command interface (pecostm32's repo is analysed in `repo-pecostm32.md`).
- 0.26y5 `ch340_class.c`: first serial with descriptors "H2750 Usb Devices" / "HeroJe Scanners Drives - H2750" (copied from a scanner project), 64 B bulk, 1024 B rings. Superseded by `cdc_class.c`.

### 2.5 Throughput facts for design
- The FPGA read path is CPU-bit-banged (section 3): about two GPIO writes per sample byte, sample buffers are `uint8`, one read pass of 16382 samples (both ADCs) per channel.
- USB TX is software drained and synchronous; the 32 KiB ring plus 512 B packets works on HS. A binary protocol would carry 16382 B/channel instead of ~19.8 kB ASCII per 3000 samples.
- No flow control or framing integrity (no length, no CRC). The IRQ-context parsing and 64 B `bufferCDC` are fragile.

## 3. MCU <-> FPGA interface as used by firmware (cross-check with `fpga-verilog-analysis.md`)

`S100 fpga_control.h:10-38`, `fpga_control.c:12-130`: F1C100s PORT E. Data bus PE0..PE7 (`FPGA_BUS_CFG_REG 0x01C20890`, direction by setting all 8 pins in/out), control PE8 = CLK (strobe, pulse low then high), PE9 and PE10 = data/cmd and read/write select (`FPGA_CTRL_CFG_REG 0x01C20894`, PE8:10 outputs, data register `0x01C208A0`):

| Operation | PE10 PE9 (bits 10:9) | Meaning |
|---|---|---|
| command write | 11 (`0x600`) | byte on bus, pulse CLK |
| command read | 10 (`0x400`) | pulse CLK, read bus (rarely used) |
| data write | 01 (`0x200`) | byte(s) on bus, pulse CLK per byte |
| data read | 00 | pulse CLK, read bus |
Multi-byte values are high byte first (`fpga_write_short/int`, `fpga_read_short`). Reading N samples = N CLK pulses with data-read selected. Version read: cmd 0x06, then short read.

Commands used by the firmware (S100) and meaning:

| Cmd | Use in firmware | Where |
|---|---|---|
| 0x01 w | sample system reset (1 = reset, 0 = release) | `fpga_do_conversion` |
| 0x04 w | "enable system", always 1 | `fpga_enable_system` |
| 0x05 r | wait for bit0 = system ready after reset | `fpga_do_conversion` |
| 0x06 r | FPGA version short (0x1432 OEM, 0x1532 AL3, 0x1632 EF2) | `fpga_get_version:196` |
| 0x0A r | bit0 = done (triggered/buffer full) | `fpga_done_conversion` |
| 0x0B w | pretrigger samples = `settriggerpoint/2` (fw_FPGA>1 only) | `fpga_do_conversion` |
| 0x0C w | total samples = `totalsamples/2` (fw_FPGA>1 only) | same |
| 0x0D w | sample-rate divider (32 bit), table `sample_rate_settings[]` | `fpga_set_sample_rate`, `variables.c:723` |
| 0x0E w | time-base counter: OEM uses `timebase_settings[]`, PECO FPGA gets `samplecount` | `fpga_set_time_base` |
| 0x0F w | 0 = trigger enabled (standard mode), 1 = trigger disabled (roll/long, and after trigger fires) | `fpga_do_conversion`, `fpga_set_long_timebase` |
| 0x14 r | trigger address (13 bit, `& 0x1FFF`) | `fpga_prepare_for_transfer:696` |
| 0x15/0x16/0x17/0x1A w | trigger channel, edge, level (0-255), mode (0 auto / 1 normal+single) | `fpga_set_trigger_*` |
| 0x1F w | sample read offset (short) | `fpga_read_sample_data` |
| 0x20 / 0x21 r | CH1 ADC1 / ADC2 sample stream | `adc1command`, `adc2command` |
| 0x22 / 0x23 r | CH2 ADC1 / ADC2 sample stream | same |
| 0x32 / 0x35 w | CH1/CH2 DC offset (short = `dc_calibration_offset[vpd] + dcoffset`) | `fpga_set_channel_offset` |
| 0x33 / 0x36 w | CH1/CH2 sensitivity relay setting 0..5 | `fpga_set_channel_voltperdiv` |
| 0x34 / 0x37 w | CH1/CH2 coupling: 1 = DC, 0 = AC | `fpga_set_channel_coupling` |
| 0x38 w | backlight PWM (short). Scale `screenbrightness*560 + 4000` (0..60000); 0xEA60 = max | `fpga_set_translated_brightness` |
| 0x3C w | touch panel address/init (FNIRSI quirk; call commented out) | |
| 0x50 w | generator output enable (written as 32-bit 1/0) | `fpga_on_off_generator` |
| 0x51 w | generator frequency word = `f * 2^32 / 50e6` | `fpga_set_generator_freq` |
| 0x52 w | generator duty word = `duty% * (2^32-1)/100` | `fpga_set_generator_duty` |
Channel-enable commands 0x02/0x03 exist in struct (`scope_functions.c:6421`, "not use?"). 0x28 (mode) and 0x28-based long-mode calls are commented out, "not supported in FPGA".

Acquisition cycle (`scope_acquire_trace_data`, `scope_functions.c:~714-880`): set trigger level (0x17) -> `fpga_do_conversion` (0x0F, reset 0x01=1, wait 0x05, program 0x0B/0x0C/time base, reset 0x01=0) -> poll `fpga_done_conversion` (0x0A, also scans touch panel) -> single mode: stop -> read trigger address (0x14) -> translate to read offset by FPGA type (OEM: <750 => +3345 else -750; AL3: <pre => +7441 else -pre; EF2: <pre => +5393 else -pre; i.e. circular buffer wrap sizes 4096-ish / 8192 / 6144 minus 1) -> per channel: cmd 0x1F offset, cmd 0x20/0x22 read `nofsamples` bytes (ADC1 into odd buffer slots), cmd 0x1F again, cmd 0x21/0x23 (ADC2 into even slots) with ADC mismatch compensation and overload flags -> measurements (min/max/avg/rms, zero crossing frequency) -> `scope_process_trigger` for software trigger refinement -> display.
Memory depth: `scopesettings.samplecount = fpgasettings.totalsamples`, `nofsamples = totalsamples/2`; menu offers 3000, 6000, 9000, 12000, 15000 (`statemachine.c:3097-3103`, `memory_deep_texts[5]`), buffers sized for `MAX_SAMPLE_BUFFER_SIZE 16382`. Trigger position 25/50/75 % = `samplecount/4`, `/2`, `-1000` (`statemachine.c:3327-3374`). Sample rates 200 MS/s down to 500 S/s (`sample_rate_settings[0..17]`, divider = 200 MHz/rate - 1; 29 entries, 19+ unused).

## 4. Bootloader, updater, SD layout, FEL, loader, risks

### 4.1 Boot chain and SD card layout (verified in code)
- Allwinner F1C100s BROM looks on SD at sector 16 (8 KiB) for an `eGON.BT0` image; the 1013D has no boot jumper so BROM falls to SPI flash/FEL otherwise.
- Constants (`updater update.h:19-30`; `bootloader fnirsi_1014d_startup.c:46-49`):
  | Sector | Byte offset | Content |
  |---|---|---|
  | 16 | 8 KiB | bootloader (eGON.BT0, up to 32 KiB; `BOOT_PROGRAM_START_SECTOR`) |
  | 80 | 40 KiB | updater (eGON.EXE; `UPDATE_PROGRAM_START_SECTOR`; also the program the bootloader loads for choice 0 in fw0.04, `PROGRAM_START_SECTOR 80`) |
  | 750 | 375 KiB | scope firmware (`DSO_PROGRAM_START_SECTOR`; 695 kB `fnirsi_1013d.bin` migration images pack all three) |
  | 708 | | input calibration (checksum words 0-1, data at byte 8..40, DC shift 40..52) |
  | 709 | | scope settings (`SETTINGS_SECTOR`, sector version 0x0100/0x0015 in S100 `variables.h:108-109`) |
  | 710 | | display + touch config; boot choice byte at sector offset 31 (`STARTUP_CONFIG_ADDRESS 0x81BFFC1F`: bootloader loads sector 710 to DRAM 0x81BFFC00); bytes 28/29 = TP X/Y swap flags |
  Chain: BROM -> bootloader (sector 16) -> updater (sector 80, the "PECO" choice) -> `load_from_SDcard(750)` loads the scope (`updater statemachine.c:~423`, `update.c:~1420-1430` copies eGON payload to 0x80000000 and jumps). The updater screen is reachable from the scope boot screen (UPDATER button, enabled in Other settings).
  `README`/manual's `dd ... seek=8` (1 KiB blocks = byte 8192) matches sector 16. SD card needs a reserved gap before the first partition (>= 1 MiB; loader requires hidden sectors >= 1023 and warns on 4 GB cards that default to 64 KiB offset).
- Bootloader fw0.04 (`Bootloader fw0.02 and fw0.03 and fw0.04\...\fw0.04 modify 1014d startup for 1013d\FNIRSI_1014D_Firmware-main\fnirsi_1014d_startup\fnirsi_1014d_startup.c`): init clocks, DRAM, display, FPGA port, `fpga_check_ready()` (loops until version == 0x1432), brightness, SD init, reads sector 710, sets display config, GT911 touch init, then boot-choice screen if byte >3 ("BOOT fw 0.04"): PECO firmware (0, load from SD sector 80, validates `eGON.EXE`, copies to DRAM 0x80000000), FNIRSI firmware (1, SPI flash 0x27000 header, length from header, load to 0x80000000), FEL (2, jump to BROM FEL at 0xFFFF0020). Timer 2 s default, touch extends to 10 s. Jump via `mov pc`. Choice 4+ shows the menu, <3 skips it.
  - Risk: fw0.04 `fpga_check_ready()` hangs forever on a custom bitstream (0x1532/0x1632/0x1732). Hence FPGA readmes require **bootloader v0.8+** (binary only, in `Guide to firmware`; source for 0.8 not in repo, only `bootloader v0.7.bin`).
- Updater (`updater 0.02o ...\fnirsi_1013d_updater`; `update.c`, `statemachine.c`, `spi_control.c`): GUI on the scope with file lists under SD folders `firmware Atlan & PECO/` (bootloader.bin, updater.bin, scope.bin, data.cal, TP_Screen.bin), `firmware Atlan & PECO FPGA/FPGA.bin`, `firmware_FNIRSI/Fnirsi 1013D.bin`, `firmware_FNIRSI_FPGA/Fnirsi FPGA.bin`, `firmware_FNIRSI_BACKUP/Fnirsi backup.bin` (`update.c:343-353`). Actions (`statemachine.c:140-340`): read SD sectors 708/710 to files (download), write bootloader to sector 16, updater to 80, scope to 750, calibration to 708, TP to 710; write FPGA.bin to SPI flash address 0; read full SPI flash (JEDEC-sized) to backup file; write `Fnirsi 1013D.bin` to SPI 0x13000; write OEM FPGA to 0; restore full backup to 0. SPI writes erase+program per 4 KB sector with read-back verify (0.02n), SPI clock reduced (0.02o), writes refused unless a backup exists (0.02g), size checked against JEDEC ID table (`spi_control.c:84-165`, e.g. 0x5E6014 Zetta ZB25VQ80A 1 MB, 0xEF4015 W25Q16 2 MB, up to 16 MB).
- Flash layout facts: stock `Fnirsi 1013D.bin` (1,735,254 B) has `eGON.BMP` at its offset 0 (flash 0x13000) and `eGON.EXE` at 0x14000 (flash 0x27000), matching the bootloader's 0x27000 read. OEM FPGA bitstream lives at flash 0. The custom AL3 bitstream (`FPGA.bin` 282,898 B, last non-FF byte at 0x450F2) is longer than the 0x13000 (77,824 B) space before the OEM firmware, so flashing it overwrites the OEM firmware/boot region in the same flash; the readme-level fix is "FNIRSI FPGA.bin + Fnirsi 1013D.bin" restore from the updater. (Inference from sizes and addresses; not confirmed on hardware. The 1 MB `fpga-1013D-1-zb25vq80.bin` backup is padded 0xFF to 1 MiB.)
- FEL: boot choice 2 jumps to BROM FEL (0xFFFF0020). Use `sunxi-fel` from `dist/`: `sunxi-fel -p write 0x7FFFFFE0 fnirsi_1013d_scope.bin exe 0x80000000` loads the scope directly in DRAM without touching the card (`dist/How_to_load_scope.txt`). The makefile builds `fnirsi_1013d_scope.bin` (FEL), `fnirsi_1013d.bin` (SD image via `mksunxi`/`flashfilepacker`).
- Loader (Windows `loader_v1.1.3.zip`, `FNIRSI_Firmware_Loader.exe`): scope in USB-MSC mode appears as removable disk; loader writes raw sectors (refuses system disks, non-512 B sectors, MBR/partition area), needs admin; loads/verifies/deletes firmware image at sector 16, reads/writes sector 710 LCD/TP config, sector 708 calibration, boot-mode byte. Changelog v1.0.0-1.1.3 in `atlan4-history-and-docs.md` section 2.2. Known failure "Device write access denied" = partition offset too small (use DISKPART to recreate partition).
- Recovery: `Recovery firmware/fnirsi_1013d.bin` (261,192 B; disconnect BNCs, runs test on all ranges, "Saved file", then USB menu). `Firmware for create backup files` (`fnirsi_1013d_fwb.bin` creates `FNIRSI_1013D_full_flash_backup.bin`, `..._tp_config.bin`, `FWB_FSI-1013.bin`; restore programs write `_main` at 0x27000 or `_full` at 0 with NO validity check).
- Risks: (1) bootloader fw0.04 hang on custom FPGA; (2) updater SPI writes can destroy OEM boot/FPGA (always take a full backup first; write blocked until backup exists); (3) FPGA bitstream write at 0 with a bitstream for the wrong variant (AL3 vs EF2 vs EP4) bricks the analog front end (needs external CH341 + 3.3 V programmer, EF2 original cannot be restored per readme); (4) MSC loader on SD with too-small hidden gap; (5) "DEV mode" and "serial mode" settings can freeze the scope; (6) settings not saved while developing in some builds (pecostm32 todo).

## 5. Latest firmware architecture (v1.00o5; v0.28a differences)

Build: C + ARM asm (`start.s`, `memcpy.s`...), NetBeans project (`nbproject/`), `Makefile`, `compile.sh`, `burn.sh`, linker script `fnirsi_1013d.ld`, tools `mksunxi`, `flashfilepacker`. Target Allwinner F1C100s (ARM926, 600 MHz CPU_CLK), DRAM at 0x80000000.

Source sizes (S100): `statemachine.c` 6629 lines (touch/state handler), `scope_functions.c` 7222 (acquisition, measurements, calibration, settings, file save/load), `menu.c` 9731 (all drawing of menus), `fpga_control.c` 1456, `display_lib.c` 1754, `usb_interface.c` 1754, `sd_card_interface.c` 1274, `variables.c/.h` 1148/1217 (global state, tables, strings), `icons.c`, `ref_and_math.c` 803, `generator.c` 195 (keyboard number entry), `DS3231.c` RTC, `touchpanel.c` GT911, `test.c` diagnostics, FatFs (`ff.c`), fonts, `ccu_control`, `spi_control`, `timer`, `power_and_battery`, `mass_storage_class.c`, `cdc_class.c`, `PC_interface.c`.
Diff vs 0.28a (lines changed): `menu.c` 1527, `statemachine.c` 1318, `scope_functions.c` 1042, `fpga_control.c` 368, `usb_interface.c` 286, `fnirsi_1013d_scope.c` 107, `variables.h` 106, `PC_interface.c` 85; new `generator.c/.h`.

Main loop (`fnirsi_1013d_scope.c:39-386`): init clock, caches, timer0, power/battery interrupts, SPI flash, FPGA port, `fpga_get_version()`, backlight 0, display, FatFs mount (hang with "SD ERROR" on failure), TP I2C init, `scope_load_configuration_data()` (sector 709/708), `usb_device_init()`, FPGA channel enable/volt/coupling/offset/trigger/time base/generator/brightness, startup picture `scope.bmp`, `scope_setup_main_screen()`, `ini_SysParam()`. Then loop: `battery_check_status()` -> acquisition (`scope_get_long_timebase_data()` in long/roll mode else `scope_acquire_trace_data()`; long memory branch is empty) -> DEV-mode overlay -> `touch_handler()` (state machine in `statemachine.c`) -> RTC clock refresh every 500 ms (`timerRTC`, `onoffRTC`, `readtime()`).
Display pipeline: `display_lib.c` software drawing into 800x480 16-bit frame buffer; `scope_display_trace_data()` renders traces from `channelNtracebuffer` (uint8, samples 0..255, 128 centre) with modes linear/dots/peak/pro, refs (REF1-8), math (sum/diff), cursors, measurements; `scopesettings`/`channelsettings` structs in `variables.h`.
Measurements: min/max/avg/rms computed in the ADC read loop (`fpga_read_adc_data`), RMS via `isqrt`, zero-crossing frequency with 16.16 fixed point and `freq_calc_data[]`; the Menu measurement list: Vmax, Vmin, Vavg, Vrms, Vpp, Freq, Cycle, Tim+, Tim-, Duty+, Duty- (+ per-channel and reference sources).
Generator: `fpgasettings.gen_*`, buttons/menu in `menu.c:6413 scope_open_generator_menu`, keyboard `menu.c:5093 scope_open_keyboard_menu`, entry handling `statemachine.c:3587-3700`; limits freq 0..`GEN_f_MAX` 10 MHz, duty 0..100. Square wave only in hardware (DDS-like phase accumulator with 50 MHz clock, threshold compare for duty). Amplitude/offset/phase stored but not written to FPGA. Hold-off value stored, FPGA write commented out.
Settings storage: calibration sector 708, settings sector 709 with version words (`SETTING_SECTOR_VERSION_HIGH 0x0100 / LOW 0x0015`), offsets `variables.h:97-105` (CH1 8, CH2 32, TRIGGER 56, OTHER 80 (40 B), CURSOR 120, MEASUREMENT 136, CALIBRATION 168, CALIBRATION_C 184, FPGA 192); persisted at power-off (`scope_save_configuration_data`, `scope_functions.c:6516`), including `USB_CH340`, `totalsamples`, `settriggerpoint`, gen_*; ref waveforms and screenshots go to SD files (waveform format `waveform_file_format for v0.026b.txt`, `WAVEFORM_FILE_VERSION 0x01000002`).
Changes vs pecostm32 base: SD-only boot with split bootloader/updater/scope; RTC; boot menu; long/roll mode; average; invert; probe ratios; DC-shift calibration; new autosetup; refs and math; keyboard; CDC; 3000..16382 samples instead of 1500/3000; FPGA-version dependent code for 3 bitstreams; generator.
Known architectural issues: `menu.c`/`statemachine.c` giant, duplicated blocks, heavy commented-out code; polling TP inside acquisition wait; ISR-context protocol parsing; global debug vars a..k.

## 6. Hardware facts gathered from code and docs

- SoC: Allwinner F1C100s. PE0-7 FPGA data bus, PE8 CLK, PE9/PE10 control; PORT C = SPI0 flash (`spi_control.c:19`: PC0 CLK, PC1 CS, PC2 MISO, PC3 MOSI; clock AHB/6 in bootloader; updater lowers speed); PORT D + DEBE = RGB LCD (800x480); PA2/PA3 software I2C bus shared by GT911 touch controller (`TP_DEVICE_ADDR 0x28`, regs 0x8040/0x8047/0x814E/0x8150, PA reset/INT sequence selects address 0x14) and DS3231 RTC (`RTC_DEVICE_ADDR 0xD0`, `DS3231.c` header: PA2 SDA, PA3 SCL). RTC: optional mod; `onoffRTC` setting; time kept in software (`hour/minute/sec`) when RTC disabled.
- Touch: GT911 with config blocks in `touchpanel.c` (two config tables, `USE_TP_CONFIG` build option), X/Y swap flags in sector 710 bytes 28/29. Display variants known from `configuration_file.txt` (timing words): 1014D new/old, 1013D v1/v2/v3 (`0x041E004A/0x041A0017` common).
- FPGA: variant detection is by version read (cmd 0x06) in `fpga_get_version()` (`fpga_control.c:196-209`): 0x1432 = OEM, 0x1532 = AL3 PECO, 0x1632 = EF2 PECO; EP4 (0x1732 per Verilog) is NOT recognised, so `fw_FPGA` would stay 0 and the OEM-specific branches break. Introduced only in v1.00o5 (diagnostic screen shows the chip). FPGA variants: Anlogic AL3S10 (8192 x 32 b sample RAM), EF2L45 (6144 x 32), Altera EP4CE6 (6144 x 32). Bitstream stored in SPI flash at address 0. See `fpga-verilog-analysis.md`.
- Analog: 4 x 8-bit ADC buses (2 ADCs per channel interleaved to 200 MS/s per channel), relays for 6 sensitivities per channel (cmd 0x33/0x36 values 0..5) plus AC/DC relay, DC offset via PWM (cmd 0x32/0x35, `dc_calibration_offset[]` per range), backlight via FPGA PWM (cmd 0x38). Battery measured with SoC ADC (`power_and_battery.c`). Optional mods in manual: DS3231 RTC; 5 V boost converter ahead of MIC29302 regulator so traces do not drift as the battery drops.
- Clocks/timing: CPU 600 MHz, ~200 us per 1000x1000 empty loop iteration comments in the bootloader, FPGA 50 MHz input for generator.
- SPI flash types supported by updater: W25Q80/16/32/64/128, GD25Q equivalents, Zetta ZB25VQ80A (1 MB FPGA flash on some boards).

## 7. TODOs and known issues (from sources, manual, folder notes)

See `atlan4-history-and-docs.md` section 3 for full list. Highlights relevant to this project:
- No `TODO/FIXME` markers; softer Slovak "treba?"/"netreba" comments only.
- PC protocol: stubbed; `usb_tx_busy` race; `bufferCDC[64]` vs 512 B packets; `SysParam` static; `#SET/#RUN/#STOP` do nothing; no flow control.
- Long memory mode and hold-off "not working"; FFT files present but not wired; Ampl/Offset/Phase generator controls not backed by hardware; EXT trigger future.
- `fpga_set_generator_freq` uses 64-bit math (software divide) on every keypress; freq limit comment says 5 MHz, constant is 10 MHz.
- EP4CE6 FPGA: address counters wrap at 8192 while RAM is 6144 deep (see FPGA notes) and the firmware does not recognise its version word.
- EF2 original bitstream restore not possible via the repo tools.
- Bootloader >= 0.8 required but only `v0.7` binary and fw0.04 source are in the repo.
- Pecostm32's old todo: ADC1/ADC2 offset cross-calibration with new FPGA, picture-view offset bug, improve calibration, SD-card compatibility (some 4 GB), USB MSC "prevent removal".
