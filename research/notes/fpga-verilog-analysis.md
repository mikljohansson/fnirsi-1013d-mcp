# FNIRSI 1013D FPGA projects (Atlan4 repo) - technical analysis

Sources (abbrev):
- `R` = `/d/src/Fnirsi1013D/Verzia 1.xxx/FPGA for 1.xxx/`
- `AL3` = `R/FPGA AL3/FPGA AL3S10/` (Anlogic AL3A10LG144C7, TD 4.6.116866)
- `EF2` = `R/FPGA EF2/FPGA EF2L45/` (Anlogic EF2L45LG144B, TD 4.6.116866)
- `EP4` = Altera_EP4CE06_v2.zip extracted to a scratch directory (`<scratch>\ep4\Altera_EP4CE06_v2\`) (Cyclone IV E EP4CE6E22C8, Quartus 22.1std Lite)
- `OLD` = `/d/src/fnirsi-1013d-ai/research/atlan4-extracted/FPGA/FPGA/` (= repo-root `FPGA.zip`; zip member md5s of zaklad.v a43616cf and FPGA.bin 87812090 equal the extracted copy; older AL3 build, 4096 deep)
- Top module: `fnirsi_1013D` (EP4: `fnirsi_1013D_EP4CE6`). Header: "Code for FNIRSI 1013D oscilloscope, Atlan and PECO" (derived from pecostm32 reverse engineered FNIRSI logic; comments such as "The original uses 16 bits" refer to the FNIRSI original).
- Line refs are `zaklad.v` of AL3 unless stated; differences in section 8.

## 1. File inventory

| Path | Notes |
|---|---|
| `R/readme` | "Install first bootloader v0.8 or higher" |
| `R/FPGA commands.txt` == `R/backup fpga/FPGA commands.txt` (diff identical) | command table with Verilog snippets (matches OLD-era code, version 0x14, see 6) |
| `R/backup fpga/fpga-1013D-1-zb25vq80.bin` == `R/FPGA AL3/fpga-1013D-1-zb25vq80.bin` (md5 e56b68bd...) | original FNIRSI AL3 bitstream, 1 MiB SPI flash dump (ZB25VQ80) |
| `AL3/{zaklad.v,zaklad.adc,zaklad.sdc,FPGA.al,al_ip/*}` | project. `FPGA.al`: Family AL3, Device AL3A10LG144C7, files zaklad.v, al_ip/pll.v, al_ip/sample_memory.v, adc, sdc; bin on; last step 2026-02-14 16:53 |
| `AL3/FPGA.{bin,bit,rbf}`, `FPGA_{gate,phy,rtl}.area`, `FPGA_phy.timing/.tsm`, `*.db`, `FPGA_inst.bid` | build outputs. `R/FPGA AL3/FPGA.bin` == `AL3/FPGA.bin` (md5 15b7fb47...) |
| `EF2/...` same layout (last step 2026-01-09 18:02); `R/FPGA EF2/EF2 fnirsi backup.bin` | original FNIRSI EF2 bitstream backup (162,011 B) |
| `R/FPGA EF2/PROGRAMER anlogic-usbjtag-master/` | anlogic-usbjtag clone (STM32F103C8T6 JTAG programmer): `flash.bin` (16,652 B ARM image; strings "USB DFU Bootloader", "MSCBoot DFU", "Zhiyuan Wan", "localhost:8000"), `al-link driver.zip`, `anlogic-usbjtag-master.zip`, `usbjtag_micro.pdf`, 3D-print box (3mf/stl/FCStd), pictures. `README.txt`: schema and flash file is stm32f103c8t6; "not yet possible to restore the original file that is in EF2" |
| `R/FPGA EP4CE06/{Altera_EP4CE06_v2.zip, fnirsi_1013D_EP4CE6.bin, rbf2bin, readme}` | `rbf2bin` = 6 MB x86-64 Linux ELF (stripped; string `srbf2bin`); readme: copy `xxxx.rbf` next to it, run, it writes `xxxx.bin` |
| `OLD` | FPGA.zip content: zaklad.v (812 lines), 8 td_*.log (2025-11-15/16), area/timing |

Readmes: AL3 and EP4 are flashed to the SPI flash with a CH341 programmer + 3.3 V converter; EF2 via the JTAG programmer + TD (Windows: must use TD_4.6.8_SP1_116866_NL.msi). Project creation time 2025-11-15 12:55:11 (all three TD projects).

## 2. Variant summary

| Item | OLD (root FPGA.zip) | AL3 | EF2 | EP4 |
|---|---|---|---|---|
| Device | AL3A10LG144C7 | AL3A10LG144C7 | EF2L45LG144B | EP4CE6E22C8 TQFP144 |
| Sample RAM | 4096 x 32 (12-bit addr) | 8192 x 32 (13-bit) | 6144 x 32 (13-bit addr, wrap at 0x17FF) | 6144 x 32 (13-bit addr, no wrap at 6143) |
| RAM impl. | 9K(FAST) x16 | 9K(FAST) x32 | 32K x6 (6/6) | M9K x24 of 30 |
| Version (cmd 0x06) | 0x1432 | 0x1532 | 0x1632 | 0x1732 |
| Generator | fixed 1 kHz divider | DDS + duty (cmd 0x50-0x52) | same as AL3 | same as AL3 |
| Pin map | AL3 pins | zaklad.adc byte-identical to OLD (cmp) | own pin map | same numbers as AL3 |
| clk_200MHz min period | 5.333 ns (187.5 MHz) | 6.234 ns (160.4 MHz) | 6.943 ns (144.0 MHz) | Fmax 166.0 MHz (85C), 183.0 (0C) |

## 3. Top-level ports and pins

Ports (zaklad.v:84-120): inputs `i_xtal` (50 MHz), `i_mcu_clk`, `i_mcu_rws` (read 0 / write 1), `i_mcu_dcs` (data 0 / command 1), `i_adc1A_d[7:0]`, `i_adc1B_d`, `i_adc2A_d`, `i_adc2B_d`; inout `io_mcu_data[7:0]`, `o_1khz_calib` (generator out / hi-Z; comment "to FPGA ext trigger", but no logic reads it); outputs `o_adc1_encA/B`, `o_adc2_encA/B`, `o_offset_1/2`, `o_relay1_1..3`, `o_relay2_1..3`, `o_ac_dc_1/2`, `o_pwm_display`. I2C port commented out ("Not implemented in this version"). 60 pads (36 in, 15 out, 9 inout).

Pins (`zaklad.adc`, all LVCMOS33; ADC data/MCU in PULLTYPE NONE; relays PULLUP; adc enc SLEWRATE FAST; o_1khz_calib FAST):

| Signal | AL3 / OLD / EP4 | EF2 |
|---|---|---|
| i_adc1A_d[0..7] | P66,65,64,60,59,58,55,54 | P35,34,33,32,31,28,27,26 |
| i_adc1B_d[0..7] | P67,68,69,70,71,72,73,74 | P38,39,40,41,42,43,44,45 |
| i_adc2A_d[0..7] | P91,90,89,88,87,86,85,84 | P62,61,60,59,58,57,56,55 |
| i_adc2B_d[0..7] | P98,99,100,101,103,104,105,106 | P73,74,75,76,77,78,81,82 |
| i_mcu_clk / rws / dcs | P1 / P2 / P3 | P105 / P106 / P107 |
| i_xtal | P23 | P128 |
| io_mcu_data[0..7] | P135,136,137,138,141,142,143,144 | P95,96,97,98,99,100,103,104 |
| o_adc1_encA / encB | P75 / P53 | P25 / P47 |
| o_adc2_encA / encB | P110 / P83 | P54 / P83 |
| o_offset_1 / 2 | P112 / P111 | P50 / P52 |
| o_pwm_display | P42 | P4 |
| o_ac_dc_1 / 2 | P129 / P132 | P92 / P93 |
| o_relay1_1,2,3 | P121,124,126 | P84,85,87 |
| o_relay2_1,2,3 | P128,125,127 | P91,86,89 |
| o_1khz_calib | P133 | P94 |

EP4 pins in `fnirsi_1013D_EP4CE6.qsf` use the same numbers as AL3 (PIN_n) - same PCB. EP4 `.sdc` is empty (two blank lines): Quartus auto-created `derive_clocks -period 1.0` for i_mcu_clk, sample_write_clock and peripheral_clock/clk_out (hence huge negative slack and "min pulse width" failures in sta.summary are artefacts); only i_xtal (20 ns) + PLL derived clocks are real.

## 4. Clocks, ADC interface, memory

### 4.1 PLL (zaklad.v:241-248, `al_ip/pll.v`, `pll.ipc`)
- In 50 MHz (`i_xtal`). AL3/EF2 PLL: REFCLK_DIV 1, FBCLK_DIV 4, CLKC0_DIV 5, CLKC1_DIV 5 (.ipc: multiplication_factor 4, division_factor 1, clock_division_factor 5) -> header: C0 = 200 MHz 0 deg, C1 = 200 MHz 90 deg (CLKC1_CPHASE 5, FPHASE 2). Feedback CLKC0_EXT via BUFG. OLD .ipc says multiplication_factor 20 (same 200 MHz outputs). Normal feedback, bandwidth Low, reset tied 0.
- EP4: altpll clk0 x4/1 phase 0, clk1 x4/1 phase 1250 ps (90 deg), duty 50, inclk 20000 ps.
- `clk0_out` = `clk_200MHz`; `clk1_out` = `clk_ADC` (named `clk_RAM` in OLD).
- `clk_50MHz`: `clockdivider #(WIDTH=1,LAST=1) peripheral_clock` on clk_200MHz (:257-267; module :8-32) - fabric-derived, not constrained in AL3 sdc.
- `sample_write_clock` (reg) toggles on `negedge clk_200MHz` when `sample_clock_enable` (:374-378).
- AL3 `zaklad.sdc`: `create_clock clk_200MHz -period 5 -waveform {0 2}`, `create_clock i_xtal -period 20`, `set_clock_latency -source 1 i_xtal`; ADC-clock generated clocks commented out. EF2 sdc: `create_generated_clock clk_200MHz -multiply_by 4`, `clk_ADC -multiply_by 4 -edge_shift 1.25`, `create_clock clk_50MHz -period 20`.

### 4.2 ADC interface
- 4 x 8-bit unsigned parallel buses: 2 ADCs ("A","B") per channel. ADC clock outputs (:361-372): on `negedge clk_ADC` when `sample_clock_enable`: `adcN_encB <= ~adcN_encB`; `adcN_encA <= adcN_encB` (so A is the complement of B after the first toggle). ch1 and ch2 get identical clocks. enc toggles at most every 5 ns => 100 MHz clock per ADC, A/B in antiphase = time-interleaved: 200 MS/s per channel effective (2 x 100 MS/s). Part numbers of the ADCs are not stated in any file examined.
- ADC bus goes directly (no input registers) to the BRAM data input, written on rising `sample_write_clock`. No I/O timing constraints exist for ADC data/clocks.
- Live reads (not from memory): cmds 0x24-0x27 return `i_adc1A/1B/2A/2B_d`.

### 4.3 Sample memory (IP)
- `al_ip/sample_memory.v` (AL3): `AL_LOGIC_BRAM` MODE PDPW, DATA_WIDTH 32 both ports, DATA_DEPTH 8192, ADDR_WIDTH 13, IMPLEMENT "9K(FAST)", REGMODE NOREG (async dual clock, no output register), `cea`=sample_write_enable, wea=1, ceb=1. Data word = `{i_adc2B_d, i_adc2A_d, i_adc1B_d, i_adc1A_d}` (bits 31:24, 23:16, 15:8, 7:0); `dob` -> `{o_adc2B_d,o_adc2A_d,o_adc1B_d,o_adc1A_d}`. Write clock = sample_write_clock; read clock = `i_mcu_clk`.
- EF2: `EF2_LOGIC_BRAM`, DEPTH 6144, ADDR_WIDTH 13, IMPLEMENT "32K". EP4: altsyncram DUAL_PORT numwords 6144, widthad 13, width 32, address_reg_b CLOCK1, unregistered output. OLD: DEPTH 4096, ADDR 12.

## 5. MCU bus protocol (F1C100s -> FPGA)

Pins: `io_mcu_data[7:0]` (byte bus), `i_mcu_clk` (strobe; comment says "active low going pulse" but all logic is `posedge i_mcu_clk`, so data are latched on the rising/trailing edge), `i_mcu_rws` (1 = MCU writes, 0 = MCU reads), `i_mcu_dcs` (1 = command byte, 0 = data byte).
- Command cycle: `dcs=1 & rws=1`: `mcu_command <= io_mcu_data` at posedge (:577-581).
- Write data cycle: `dcs=0 & rws=1`: case on `mcu_command` (:625-831), bus sampled at posedge.
- Read data cycle: `dcs=0 & rws=0`: result registered into `o_mcu_data` at posedge (:837-923); bus driven by FPGA whenever `rws=0` (`io_mcu_data = i_mcu_rws ? 8'bZ : o_mcu_data`, :929).
- Byte index `mcu_state` (reg[3:0], :588-598): cleared while `dcs==1`; incremented at each posedge with `dcs=0` for multi-byte commands: writes (rws=1) 0x0B,0x0C,0x0D,0x0E,0x1B,0x1F,0x32,0x35,0x38,0x3C,0x51,0x52; reads (rws=0) 0x06,0x14,0x41. Non-blocking, so first data byte sees state 0 (high byte first), then 1, 2, 3. Single-byte commands ignore it. (OLD list lacks 0x0B,0x0C,0x1B,0x51,0x52.)
- Memory read pointer `sample_read_offset` (13 bit): auto-increment (blocking `=`, :607) on every data-read strobe of cmds 0x20-0x23; loaded by write cmd 0x1F (high byte `io_mcu_data[5:0]` into [12:8] (truncated to 5 bits), then low byte) (:610-619). BRAM output is unregistered/synchronous to i_mcu_clk, so the byte returned on a strobe corresponds to the address presented at that edge (the previous pointer value) - expect one-word pipeline lag (inference; not verified against firmware). EF2: pointer wraps at 0x17FF (:626).
- `sample_read_enable` (0x0F) is dead: `sample_read_address = sample_read_enable ? sample_read_offset : sample_read_offset` and same for the clock (:548,:550).
- Multi-byte values are always high byte first.

## 6. Command table (verified against AL3 Verilog)

`FPGA commands.txt` is the older table: it reads version 0x14, shows 0x1F with `[4:0]`, has 0x33/0x36 without the `<6` guard, and has no 0x0B/0x0C comments in OLD code; otherwise it matches the AL3 code. It does list 0x0B/0x0C/0x1B/0x50-0x52, so it is newer than OLD zaklad.v. Semantics below are from the Verilog.

Reads (rws=0):

| Cmd | Semantics |
|---|---|
| 0x05 | bit0 = `sample_system_state`: set after 7 write-clock edges while reset=1 (`sample_system_reset_count==6`), cleared when reset=0 (:843, :557-572) |
| 0x06 | FPGA version, 2 bytes: state0 = 0x15 (AL3) / 0x16 (EF2) / 0x17 (EP4) / 0x14 (commands.txt, OLD); state1 = 0x32 (:847-855) |
| 0x0A | bit0 = `~sample_write_enable` (1 = capture done) (:860) |
| 0x14 | trigger address, 2 bytes: `[12:8]` then `[7:0]` (:864-872; OLD `[11:8]`) |
| 0x18 | bit0 = `sampling_triggered` (comment: unused by firmware) |
| 0x20 / 0x21 | memory: ch1 ADC A / ch1 ADC B byte; auto-increment pointer |
| 0x22 / 0x23 | memory: ch2 ADC A / ch2 ADC B byte; auto-increment pointer |
| 0x24-0x27 | live pins ch1A, ch1B, ch2A, ch2B |
| 0x41 | read back `touch_panel_address` (16 bit, hi then lo) |

Writes (rws=1):

| Cmd | Semantics |
|---|---|
| 0x01 | `sample_system_reset <= d[0]` (1: clear write address, time-base counter, trigger flags/previous/data, `trigger_address`=0, `trigger_read_address`=`pretrigger_samples`, `sample_write_enable`=1) |
| 0x0B | `pretrigger_samples` 13 bit (2 B), default 750 ("set_trigger_point") |
| 0x0C | `total_samples` 13 bit (2 B), default 1500 (pre + post trigger) |
| 0x0D | `sample_rate_divider` 32 bit (4 B) |
| 0x0E | `time_base_set` 32 bit (4 B): auto-mode timeout, in write-clock cycles |
| 0x0F | `sample_read_enable` (no function) |
| 0x15 | `trigger_channel` (0 = ch1 ADC A, 1 = ch2 ADC A) |
| 0x16 | `trigger_edge` (0 rising, 1 falling) |
| 0x17 | `trigger_level` 8 bit |
| 0x1A | `trigger_mode` (0 auto, 1 normal) |
| 0x1B | `hold_on_divider` 32 bit (4 B): stored, never used |
| 0x32 / 0x35 | ch1 / ch2 DC offset 16 bit -> PWM compare |
| 0x33 / 0x36 | ch1 / ch2 sensitivity relays; only when data < 6: `relayX_1=d[0]`, `relayX_2=~d[1]`, `relayX_3=~d[2]` |
| 0x34 / 0x37 | ch1 / ch2 `ac_dc = d[0]` |
| 0x38 | display brightness 16 bit -> PWM compare |
| 0x3C | `touch_panel_address` 16 bit scratch (read via 0x41); no I2C logic exists |
| 0x50 | `gen_out_enable = d[0]` (default 1) |
| 0x51 | `clk_out_divider` 32 bit = DDS phase increment (default 85899) |
| 0x52 | `clk_out_duty` 32 bit = duty threshold (default 0x80000000) |

Dead/odd items: wires `trigger_rise_enable`/`trigger_fall_enable` and register `trigger_detect_enable` have no effect; `cur_less_equal_lev`, `trigger_rise_detect`, `trigger_fall_detect` are implicit nets; BUFFER_BITS/BUFFER_SIZE localparams unused; `mcu_state` assigned `3'h0`.

## 7. Datapath logic

### 7.1 Sample-rate divider / time base (:384-394, :415-423)
```
always @(negedge clk_200MHz) sample_rate_counter <= sample_clock_enable ? 1 : sample_rate_counter + 1;
assign sample_clock_enable = sample_rate_divider < sample_rate_counter;   // 32-bit
```
Counter runs 1..N+1, enable pulses every N+1 cycles of 200 MHz (N = divider). Each pulse toggles `sample_write_clock` and the ADC enc -> write rate = 100/(N+1) MHz (N=0: enable is permanently 1, 100 MHz; code comment: "the plus one is needed ... without it the maximum would be 50 MHz"), effective interleaved rate 200/(N+1) MS/s per channel; 32-bit divider gives practically unlimited slow rates (OLD: counter<=0 and compare `counter+1`, identical behaviour).
`time_base_cnt` (32 bit) counts `sample_write_clock` posedges from reset; `time_base_timeout = time_base_cnt > time_base_set`.

### 7.2 Write address, enable, half-way
- `sample_write_address` 13 bit: +1 per write clock, 0 on reset (:400-407). AL3 wraps at 8192; EF2 at 6144 (0x17FF); EP4 at 8192.
- `sample_write_enable`: 1 on reset, 0 when `sampling_enable==0` (:429-436).
- `sampling_half_way <= (sample_write_address >= pretrigger_samples) | itself` (:509-516): trigger armed only once the pre-trigger depth is filled (OLD hard-coded 750).

### 7.3 Trigger (:444-475, :478-490, :520-542)
- `trigger_data <= trigger_channel ? i_adc2A_d : i_adc1A_d`; `trigger_previous <= trigger_data`, on posedge `sample_write_clock` (only ADC "A" of the chosen channel, 8-bit unsigned; OLD used posedge clk_RAM).
- `prev_less = trigger_previous < level` (strict); `cur_ge = trigger_data >= level`; `cur_lt = trigger_data < level`. Rise = `~edge & prev_less & cur_ge`; fall = `edge & ~prev_less & cur_lt`.
- Latch: `always @(posedge clk_200MHz)`, if `trigger_check_enable = ~sampling_triggered & sampling_half_way`: `sampling_triggered |= rise | fall`. Reset clears. Unsynchronised crossing from the write-clock domain into the 200 MHz domain (benign for N>0, marginal at N=0).
- `trigger_read_address` (13 bit): reset -> `pretrigger_samples`; after half-way and while `sampling_enable`, +1 per write clock.
- `trigger_address`: reset 0; follows `trigger_read_address` until triggered (frozen); in auto mode on timeout it is forced to `pretrigger_samples` (comment: firmware time-base values unreliable, hence fixed fallback).
- Stop (:444-447): `sampling_enable = ~((sampling_triggered && addr_delta >= total_samples - pretrigger_samples) || (~trigger_mode && time_base_timeout))`; `addr_delta = trigger_read_address - trigger_address` (13-bit mod 8192; EF2 explicit mod 6144). OLD: `trigger_read_address >= trigger_address + 750`.
- Normal mode has no timeout (waits, buffer keeps wrapping); auto mode stops on time-base timeout. MCU computes read start from trigger address (firmware).

### 7.4 Generator (:65-80, :271-281)
`freq_generator_dds_pwm` on clk_50MHz: `phase_acc += phase_inc; clk_out <= (phase_acc < duty_threshold)`. f = phase_inc * 50 MHz / 2^32 (step 11.64 mHz; 85899 -> 1.0000 kHz), duty = threshold/2^32. Output pin `o_1khz_calib`: `gen_out_enable ? clk_1KHz : 1'bZ` (:280). Registered at 50 MHz => 20 ns edge jitter; theoretical max 25 MHz. OLD: `clockdivider #(16,24999)` on clk_50MHz = fixed 1 kHz, always driven (comment: original used 32 bits/25000 giving wrong frequency).

### 7.5 PWM outputs, relays
- `pwmcounter` (:37-61): 16-bit counter on clk_50MHz, `pwm <= count < pulsewidth`, no update at count==LAST.
  - offsets: LAST=1999 -> 2000 cycles = 40 us = 25 kHz PWM, ~11-bit resolution (values >= 2000 = 100 %); external filter makes the DC offset (no DAC).
  - display brightness: LAST=59999 -> 1.2 ms = 833.3 Hz, duty = value/60000. Alternate commented/earlier code: `display_brigthness >> 5`, LAST 2047/1861 (:331-338; EP4 .v.bak still uses `>>5`).
- Relays: 3 bits per channel from cmd 0x33/0x36 (bit1/bit2 inverted), AC/DC direct from 0x34/0x37; power-on 0.

## 8. Variant differences

### AL3 vs EF2 (diff -> 89 lines)
- `localparam MEM_END_ADDR = 13'h17FF` (EF2 zaklad.v:226); wrap in write address (:406), `trigger_read_address` (:538-543), `sample_read_offset` (:626).
- `addr_delta` modulo 6144 (:454-460).
- Version low-hi byte 0x16 (:871) vs 0x15. `//wire clk_1KHz` commented in EF2 (still used via implicit net); `wire clk_ADC` present only in AL3. Different pins/sdc (sec. 3/4.1). Memory IP 32K, 6144.

### AL3 vs EP4
- Port/IP names (`inclk0/c0/c1`, `data/wraddress/wrclock/wren/q/rdaddress/rdclock`), version 0x17 (:846), `sample_clock_enable` without parentheses.
- Memory 6144 x 32 but no wrap logic: RAM addresses 6144-8191 do not exist while counters run to 8191 (BUFFER_BITS=13 modulo 8192). When write/trigger addresses land in 6144-8191 (e.g. long normal-mode wait) written samples are lost and reads undefined: likely corrupted captures (inference from source; not tested).
- `.v.bak` is the earlier version (`display_brigthness >> 5`, `sample_rate_counter <= 0`).
- Empty `.sdc` (default 1 ns derived clocks).

### AL3 vs OLD (zaklad.v 968 vs 812 lines)
OLD: 12-bit addresses/4096 depth; fixed 750 pre/post trigger; no regs/commands 0x0B/0x0C/0x1B/0x50-0x52; mcu_state list shorter; no wrap logic; `trigger_read_address` initial 750 and `+1` whenever half-way (no `sampling_enable` gate); trigger regs clocked on clk_RAM; fixed 1 kHz output; version 0x14.
Original FNIRSI FPGA has no source in the repo; the version 0x1432 in commands.txt/OLD probably reflects it (unverified).

## 9. Binaries

| File | Size | Notes |
|---|---|---|
| `backup fpga/fpga-1013D-1-zb25vq80.bin` (orig AL3) | 1,048,576 | 32 x 0xFF then sync `cc55aa33 f0000006 18006c31 0bb7c200 06d1...` at offset 32; payload ends at 282,866, 765,710 B erased tail. No printable strings / device-ID text (binary frame stream; only header words) |
| `FPGA AL3/FPGA.bin` | 282,898 | same header (`18006c31`), 32 FF lead and 32 FF tail; same payload end offset (282,866 non-FF) as the original; 47,494 bytes differ from the original (different design content, same frame structure) |
| `AL3/FPGA.rbf` | 282,898 | bit-reversed .bin (`33aa55cc`); `.bit` 285,341: ASCII header "Anlogic Infotech Corporation ASCII Bitstream", Version 4.6.116866, Design fnirsi_1013D, Architecture al3_10, Package LQFP144, Date 2026/2/14 16:52, CRC 10110000110011111000101111111000, USER CODE 00000000010101100000000000000000 |
| `EF2 fnirsi backup.bin` (orig EF2) | 162,011 | sync at 32: `cc55aa33 f0000006 03004c37 17a5c200`; ends 0xEE fill; device word differs from AL3 |
| `EF2/FPGA.bin` | 162,011 | same size as the backup; sync at offset 48 (extra 16-byte preamble `ffdd66bb22ffffff82ff...` = bit-reversed "Feature code" FFDD66BB22FFFFFF82FFFFFFFFFFFFFF13A7 from the .bit); 49,178 bytes differ. .bit: Architecture ef2_4, EF2L45LG144B, Date 2026/2/14 16:36, CRC 10110000011101101010111000110000, USER CODE 00010000111110100000000000000000 |
| `EP4/fnirsi_1013D_EP4CE6.bin` | 117,247 | Cyclone IV rbf, bit-reversed (`ff*32` + `56efefef...`); zip's `output_files/fnirsi_1013D_EP4CE6.rbf` is 116,871 B (rev(rbf) is not byte-identical: only 57,825 bytes equal, so the shipped bin is a different build than the rbf in the zip); .sof 358,734 B (2026-02-14 17:16) |
| `PROGRAMER/flash.bin` | 16,652 | STM32F103 (vector table 0x20005000 / 0x08001665) JTAG-adapter firmware |

Comparison: orig AL3 vs custom AL3 share chip/ID words and frame layout, so the custom file is a drop-in for the SPI flash (programmed with CH341; bootloader >= 0.8 required). The orig EF2 image cannot be read back by the supplied JTAG programmer (README.txt) - the backup exists though. No bin carries version/vendor text strings.

## 10. Resource utilization and timing

| | OLD | AL3 | EF2 | EP4 |
|---|---|---|---|---|
| LUT | 533/8640 (6.2 %) | 620/8640 (7.2 %) | 659/4480 (14.7 %) | 853 comb / 6272 (14 %) |
| LE | 543 | 658 | 966 | 900/6272 |
| Reg | 366 (4.2 %) | 471 (5.5 %) | 473 (10.6 %) | 474 (8 %) |
| BRAM | 16/48 9k (33 %) | 32/48 9k (66.7 %) | 6/6 32k (100 %); 0/12 9k | 24/30 M9K; 196,608/276,480 bits (71 %) |
| DSP | 0/3 | 0/3 | 0/15 | 0/30 |
| PLL | 1/2 | 1/2 | 1/1 | 1/2 |
| Pads | 60/93 | 60/93 | 60/109 | 60/92 |
| clk_200MHz min period | 5.333 ns (187.5 MHz) | 6.234 ns (160.4 MHz) | 6.943 ns (144.0 MHz) | 166.03 MHz (1200 mV 85C), 183.02 MHz (0C) |
| Setup violations | 18, worst -0.333 ns | 115, worst slack -5.234 (clkc[0]/clkc[1] aliases 97.7/103.4 MHz) | 107, worst -5.943 (91.4/95.0 MHz) | negative on all (artefact of unconstrained 1 ns clocks) |
| Hold | 0 errors | 0 (min +0.467) | 0 (min +0.384) | -0.145 ns on clk[0] (85C slow) |

Critical path in all AL builds: 32-bit `sample_rate_counter` increment + `sample_rate_divider < counter` compare (`add0/ucin.. -> lt0/..` chain, 14-17 levels) on negedge clk_200MHz. STA coverage 8-9 %; clk_50MHz, i_mcu_clk, sample_write_clock are unconstrained; no I/O delays. Source: `FPGA_phy.area`, `FPGA_phy.timing` (summary at end), `FPGA_phy.tsm`; EP4: `output_files/*.fit.summary`, `*.sta.summary`, `*.sta.rpt`.

## 11. Limits and improvements

Sample depth:
- Memory: EF2L45 has exactly 6 x 32 Kbit blocks, all used = 6144 x 32 -> hard cap. AL3A10 uses 32 of 48 9k blocks (8192 x 32 built from 32 blocks of 8192 x 1) plus 2 unused 32k blocks: theoretical ~12,288 deep (9k only), ~15.8 k including 32k blocks. EP4CE6: 30 M9K x 9216 = 276,480 bits; 6144 uses 24; 8192 would need 32 M9K in x1 geometry (does not fit), about 7168 x 32 (28 M9K, 1K x 9 organisation) is the max.
- RTL/protocol: all address registers are 13 bit (`pretrigger_samples`, `total_samples`, `trigger_address`, `sample_read_offset`; high byte `[4:0]`) -> max 8192 words even with more memory; extending requires wider regs (spare bits exist in the high byte) and firmware changes.
- Each word = 4 bytes (2 ADC x 2 channels): per channel 2 samples per word (16,384 interleaved samples/ch for AL3, 12,288 EF2/EP4).
- Readback: one byte per strobe, per ADC command, over 8-bit bus.

Sample rate:
- ADC clock = 100 MHz max toggle rate (200 MHz / 2), A/B antiphase -> 200 MS/s per channel; bounded by PLL output (200 MHz = 50 MHz x 4 / 5 with VCO), ADC max clock, and fabric timing - the 200 MHz domain closes only at 144-187 MHz because of the 32-bit counter/comparator.
- Improvements: pipeline divider (down-counter with registered terminal count, or 16-24-bit); real SDCs (ADC enc as generated clocks, ADC data input delay, i_mcu_clk as clock; remove unconstrained clk_50MHz); use IO registers/ODDR for enc outputs and ADC input registers (`#ireg 0`, `#oreg 14`); fix EP4 wrap (add MEM_END_ADDR like EF2) or size to 7168/8192; use spare AL3 BRAM for deeper memory; trigger from both ADC A/B or add sub-sample interpolation (currently A only, one sample per write clock); synchronise CDC trigger signals; dead items (`hold_on_divider`, `sample_read_enable`, touch address) may be repurposed; clock DDS from 200 MHz for lower jitter/higher generator frequency.

## 12. Gaps
- Not verified on hardware: read pipeline lag, EP4 overflow behaviour; ADC part numbers and firmware use of version bytes are not in these sources.
