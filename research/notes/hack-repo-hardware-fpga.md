# FNIRSI 1013D/1014D hack repo: hardware, schematics, FPGA digest

Source repo: `D:\src\FNIRSI-1013D-1014D-Hack` (pecostm32). Paths below are relative to it.
Legend: **[F]** fact from repo files. **[I]** my inference. **[V]** read from schematic image (pin numbers read from PNG; wire tracing moderate confidence).
Repo readme: hacking moved to https://github.com/pecostm32/FNIRSI_1013D_Firmware (open firmware).

---------------------------------------------------------------------
## 1. FPGA interface (F1C100s <-> FPGA)

Sources: `FPGA explained/FPGA explained.txt`, `readme.txt`, `Startup and signal data read sequence.txt`, `parameter_analysis.txt`, 22 per-timebase `*-div_signal_data_read_sequence.txt`; cross-checked against the author's later firmware `Test code/fnirsi_1013d_scope/fpga_control.c/.h` and `variables.c`.

### 1.1 Bus (8-bit parallel, bit-banged on F1C100s Port E) [F]
| Signal | F1C100s pin | FPGA pin (1013D) | Notes |
|---|---|---|---|
| DB0..DB7 | PE0..PE7 (QFN pins 49,48,47,46,45,44,43,42) | 135,136,137,138,141,142,143,144 | bidirectional |
| FPGA_CLK | PE8 (pin 41) | 1 | strobe, idles HIGH, rising edge active |
| FPGA_RWS | PE9 (pin 40) | 2 | 0 = read, 1 = write |
| FPGA_DCS | PE10 (pin 39) | 3 | 0 = data, 1 = command |
PE11 = POWER_ON, PE12 = BATTERY_CHARGING (CPU side only, 1013D). Registers: PE_CFG0 0x01C20890 (bus dir, 0=in 0x11111111=out), PE_CFG1 0x01C20894 (ctrl lines, `|=0x111`), PE_DAT 0x01C208A0.
No ready/busy pin. Handshake is by polling FPGA registers (cmd 0x05 bit0, 0x0A bit0, version 0x06). CPU ~600 MHz (comment in fpga_control.c); each byte = two GPIO writes plus a read.

### 1.2 Transaction protocol [F, fpga_control.c]
- Command write: RWS=1, DCS=1 (PE_DAT bits 9,10 = 0x600), bus out, cmd on DB, CLK low then high.
- Data write: RWS=1, DCS=0 (0x200), bus out, byte on DB, CLK pulse. Multi-byte values MSB first, one CLK per byte (short 2, int 4).
- Data read: write command first, then bus in, RWS=0, DCS=0 (0x000). Each CLK pulse presents next byte; CPU samples DB right after the rising edge. Bulk read = loop {pulse CLK; read PE_DAT & 0xFF}.
- `fpga_read_cmd` (RWS=0, DCS=1 = 0x400) exists, apparently unused.

### 1.3 Command table [F unless marked]
| Cmd | Dir/len | Meaning |
|---|---|---|
| 0x01 | W1 | Sample-system reset: write 1, wait flag (0x05), write 0 |
| 0x02 / 0x03 | W1 | CH1 / CH2 enable (0/1). Stock writes only at startup |
| 0x04 | W1 (=1) | "Enable system", at setup/hw check |
| 0x05 | R (twice) | Wait flag, loop until bit0==1, read twice vs glitch (logs 05,05) |
| 0x06 | R2 | FPGA version, must be 0x1432. Stock checks only in hardware-check mode (flash 0x001FE000 == 0x9086 skips). New fw loops until it matches |
| 0x0A | R1 | Triggered / buffer full, bit0 |
| 0x0D | W4 | Stock: roll mode value (00000027 then 000007D0) for 100 ms..50 s/div. New fw: sample-rate divider N, fs = 200 MS/s / (N+1) (see 3.1) |
| 0x0E | W4 | 32-bit "trigger time/div" parameter, table in 1.7 |
| 0x0F | W1 | 0 = trigger enabled during conversion, 1 = disabled (set to 1 after trigger) |
| 0x14 | R2 | Trigger / write-pointer, 12 bits (&0x0FFF), +2 in new fw |
| 0x15 | W1 | Trigger source 0=CH1, 1=CH2 |
| 0x16 | W1 | Trigger edge 0=rising, 1=falling |
| 0x17 | W1 | Trigger level (ADC code, 128 mid). Stock logs show 0x27 or 0x54 per timebase; new fw writes real level |
| 0x1A | W1 | Trigger mode 0=auto, 1=normal/single |
| 0x1F | W2 | Read start address, written before each ADC stream read. New fw: ptr<750 ? ptr+3345 : ptr-750 (= ptr-750 mod 4096, trigger at sample 750 of 1500) |
| 0x20 | R n | CH1 ADC core 1 stream (1500 B; 750 at 20/50 ms). Stock obtains cmd byte from special IC id 0x0C |
| 0x21 | R n | CH1 ADC core 2 stream (hard-coded), stock reads it only at 10/25 ns/div |
| 0x22 / 0x23 | R n | Same for CH2 (special IC id 0x0D gives 0x22) |
| 0x24 / 0x26 | R10 | Roll mode CH1 / CH2, 10 bytes per read |
| 0x28 | W1 | Mode: 0 short timebases (<100 ms), 1 roll (>=100 ms) |
| 0x32 / 0x35 | W2 | CH1 / CH2 vertical offset (per-range calibrated value, flash 0x001FD000) |
| 0x33 / 0x36 | W1 0..5 | CH1 / CH2 input range (relay code) |
| 0x34 / 0x37 | W1 | CH1 / CH2 coupling: 1=DC, 0=AC |
| 0x38 | W2 | Backlight PWM. Stock 0xEA60 (60000) full. New fw `brightness(0..100)*560+4000` |
| 0x3C | W2 | Value from special IC id 0x16 at startup (stock log 0x8150-looking result 81 50). New fw writes 32431. Purpose unknown |
| 0x41 | R2 | Returns 0x8150 (GT911 point-1 X-low register). Stock loops forever if not |
| 0x64 / 0x65 | cmd, sent twice | Special IC: prepare read / prepare write |
| 0x66 | cmd | Special IC: start |
| 0x67 | R1 | Special IC ready flag (0x01) |
| 0x68..0x6E | W/R1 each | Special IC packet bytes |

Range code and offset (stock, 10x probe) [F]:
| V/div | code | CH1 0x32 | CH2 0x35 |
|---|---|---|---|
| 500mV | 5 | 02 E0 | 04 99 | (same as 1V, x2 in software)
| 1V | 5 | 02 E0 | 04 99 |
| 2V | 4 | 02 E7 | 04 9F |
| 5V | 3 | 02 D3 | 04 96 |
| 10V | 2 | 02 DA | 04 9C |
| 25V | 1 | 02 B9 | 04 8B |
| 50V | 0 | 02 B9 | 04 8D |
1x probe 50 mV..5 V, 100x 5 V..500 V. Hardware sensitivity stops at 1 V/div (code 5); 500 mV is software gain. New-fw `signal_adjusters[]` 7258042,7341950,7551720,7551720,7719536,7719536,15439072 (last = software 50 mV).

### 1.4 "Special IC" / parameter IC (cmd 0x64-0x6E) [F]
FPGA block with an I2C-like link to an external 24C04-type part (1013D `FPGA_EEPROM_SDA/SCL`, FPGA pins 38/39, N24C04/24LC04BT; 1014D "UNKOWN IC", `FPGA_SPECIAL_IC_SDA/SCL` pins 33/34). parameter_analysis.txt: "not a storage device, but some sort of translator".
Transaction: write: `0x65,0x65`; write `0x68..0x6E`; `0x66`; read `0x67` until 1. Read: `0x64,0x64`; `0x66`; read `0x67`; read `0x68..0x6E`.
Packet (verified from logs): `0x68` XOR mask 0x00/0xFF, toggles every write, echoed in reply; `0x69` = (id<<2 | count) ^ mask; `0x6A` = sum of the other six bytes mod 256 (checked: 00+28+69+96+99+13 = 0x1D3 -> D3); `0x6B..0x6E` value MSB..LSB ^ mask, unused bytes 69,96,99 pre-mask. Id 0x14 input always 0xED. Reply `0x69` = 0xAA (mask 00) / 0x55 (mask FF); rest of reply not decoded [I].
IDs: 0x0A 16-bit value to write after FPGA cmd fetched with id 0x14 (startup); 0x0B fixed answers for inputs 0..6 = AD,AF,B4,B4,B8,B8,B8 (after 0x0C/0x0D); 0x0C returns CH1 read cmd (0x20); 0x0D CH2 (0x22); 0x0E 32-bit, same value; 0x10 backlight 0..0x64 -> up to 0xEA60; 0x11 called regularly (input >>4, +2, 12 bit; pairs with FPGA cmd 0x14); 0x14 returns an FPGA command; 0x16 -> FPGA cmd 0x3C; 0x0F,0x12,0x15 unknown.
**Not required**: the author's later firmware has no special-IC traffic and works on hardware. Stock roll mode also stops using it. Needs a delay between write and read of the IC (readme 2-Aug-2021).

### 1.5 Startup sequence (stock) [F]
1. `0x38` W `EA 60` (second program loader), then `0x38` W `00 00` (display off).
2. `0x02`=1, `0x03`=1, `0x33`=0, `0x36`=0 (50 V/div), `0x34`=1, `0x37`=1 (DC), `0x04`=1.
3. Special IC: initial read; id 0x0A, id 0x14 (input 0xED) -> value written as FPGA command; id 0x16 -> `0x3C` W `81 50`; id 0x10 -> `0x38` brightness `EA 60`.
4. `0x0D` W `00 00 00 27`, `0x15`=1, `0x16`=0, `0x17`=0x27, `0x1A`=0, `0x32`=`02 43`, `0x35`=`04 C6`.
5. Optional `0x06` == 0x1432 and `0x41` == 0x8150 checks. Then acquisition loop.

### 1.6 Short-timebase cycle [F]
```
0x17 W trig_val
0x0E W4 timebase_param
0x0F W 0 ; 0x28 W 0 ; 0x01 W 1          arm/reset
0x05 R,R until bit0 = 1
0x01 W 0
0x0A R until bit0 = 1                   triggered/full
0x0F W 1
0x14 R2 (07 D5 in logs)                 -> special IC id 0x11 (stock)
per enabled channel: 0x1F W2 val ; 0x20 R n [; 0x1F W2 val ; 0x21 R n]   (CH2: 0x22/0x23)
```
Stock repeats this 10 times per full sequence (2 times at 20/50 ms). Suggests multi-shot averaging or equivalent-time reconstruction [I].

### 1.7 Per-timebase table (stock; from the trace files) [F]
Idx 0 = 50 s ... 29 = 10 ns. Reads are per channel per cycle.
| Idx | t/div | 0x0E W4 | 0x17 | 0x1F W2 | Streams | Bytes/read | Cycles/seq |
|---|---|---|---|---|---|---|---|
| 29 | 10 ns | 00 06 45 DC | 27 (1 per 10 cycles) | 00 C7 | 0x20+0x21 / 0x22+0x23 | 1500 | 10 |
| 28 | 25 ns | 00 06 45 DC | 27 (1 per 10) | 00 AC | 0x20+0x21 / 0x22+0x23 | 1500 | 10 |
| 27 | 50 ns | 00 06 45 DC | 27 | 00 86 | 0x20 / 0x22 | 1500 | 10 |
| 26 | 100 ns | 00 06 45 DC | 27 | 00 31 | 0x20 / 0x22 | 1500 | 10 |
| 25 | 250 ns | 00 06 45 DC | 27 | 0F 31 | 0x20 / 0x22 | 1500 | 10 |
| 24 | 500 ns | 00 06 45 DC | 54 | 0D EC | 0x20 / 0x22 | 1500 | 10 |
| 23 | 1 us | 00 03 25 DC | 54 | 0D EC | same | 1500 | 10 |
| 22 | 2 us | 00 01 45 DC | 54 | 0D EC | same | 1500 | 10 |
| 21 | 5 us | 00 00 55 DC | 54 | 0D EC | same | 1500 | 10 |
| 20 | 10 us | 00 00 55 DC | 54 | 0D EC | same | 1500 | 10 |
| 19 | 20 us | 00 00 25 DC | 54 | 0D EC | same | 1500 | 10 |
| 18 | 50 us | 00 00 15 DC | 54 | 0D EC | same | 1500 | 10 |
| 17 | 100 us | 00 00 0B B8 | 54 | 0D EC | same | 1500 | 10 |
| 16..12 | 200 us..5 ms | 00 00 09 C4 | 54 | 0D EC | same | 1500 | 10 |
| 11 | 10 ms | 00 00 07 08 | 54 | 0D EC | same | 1500 | 10 |
| 10 | 20 ms | 00 00 03 20 | 27 | 00 0A | 0x20 / 0x22 | 750 | 2 |
| 9 | 50 ms | 00 00 03 20 | 27 | 00 0A | same | 750 | 2 |
| 8..0 | 100 ms..50 s | 0x0D = 00 00 07 D0 (00 00 00 27 first after touch) | - | - | 0x24 / 0x26, 0x28 W 1 before each | 10 | roll |
Details: `0x17` appears 11x per 10 cycles for 50 ns..10 ms (twice at start), 1x per 10 cycles at 10/25 ns, 3x per 2 cycles at 20/50 ms. At 10/25 ns the 0x1F write precedes each of the four stream reads. Roll sequence: `0x41` R once, then loop {`0x0D` W 7D0, `0x28` W 1, `0x24` R10, `0x26` R10}, no special IC. Constant bytes in the logs (0xB0, 0x70, 0x28, 0x20) are captured sample data.
The 0x0E values equal the new-fw `timebase_settings[]` (1800, 2500, 3000, 5596, 9692, 21980, 83420, 206300, 411100); new fw uses 1800 for all <=10 ms/div. Meaning of 0x0E undecoded (capture length / hold-off counter?) [I].

### 1.8 parameter_analysis.txt conclusion
The special IC is a "translator" (list in 1.4). The stock firmware uses it to look up the stream command, pointer fix-up, brightness and 0x3C. readme.txt: "Would be interesting to see if the special IC is really needed" -> later firmware shows it is not.

---------------------------------------------------------------------
## 2. Schematics (EasyEDA, drawn by pecostm32; capacitor values partly guessed)

Files: `Schematics/1013D/Scope_{Processor,Data_Acquisition,Analog_Input,Power_Supply}.png`, `PDF/Schematic_Scope 1013D_2023-01-07.pdf`, `EASYEDA/SCH_Scope 1013D_2023-01-07.json`. `Schematics/1014D/Scope_1014D_{Processor,Data_Acquisition,ADC_DAC,Analog_Input,User_Interface,Power_Supply}.png`, PDF 2023-01-06, EasyEDA JSON 2022-05-10 and 2023-01-06.

### 2.1 1013D sheet 1: Processor
- **U1 Allwinner F1C100s** QFN-88, ARM926EJ-S, built-in DDR1 [F datasheet]. 24 MHz crystal on HOSC. Rails: VCC-IO 3V3, VCC-DRAM 2V5 (SVREF from R28/R29 1K divider), VDD-CORE 1V2, analog 3V. RESET pin 70 pulled up (R30 10K).
- **USB**: USB-C (USB-310D-B-SU) on USB-DP/DM. Mass-storage mode and FEL. 1014D uses a USB-A footprint.
- **SPI flash U2 W25Q32FVSSIGTR (4 MB)** on SPI0: PC0 CLK (59), PC1 CS (60), PC2 MISO (61), PC3 MOSI (62). 2x3 header J1 (1 CS, 2 GND, 3 MISO, 4 3V3, 5 CLK, 6 MOSI) for in-circuit programming. Older boards W25Q16 (2 MB).
- **SD/TF** HY-TF1007F on SDC0: PF0 D1, PF1 D0, PF2 CLK, PF3 CMD, PF4 D3, PF5 D2 (pins 58,57,56,55,54,53), 10K pull-ups. PF2/PF4 double as UART0 RX/TX (debug).
- **Touch P1 (6-pin FPC)** [V]: PA0 (66) RESET, PA1 (65) INT, PA2 (64) SDA with 10K pull-up, PA3 (63) SCL with 10K pull-up, 3V3, GND. GT911-family, I2C address 0x5D or 0x14 chosen by INT level at reset [F GT911 guide].
- **LCD P3** (50-pin 0.5 mm FPC; Waken/WKS RGB panel, `IC's/lcd_pinout.png`): DE mode, **only RGB565 wired**: panel pins 12-16 B7..B3 <- LCD_D7..D3; 20-25 G7..G2 <- LCD_D15..D10; 28-32 R7..R3 <- LCD_D23..D19; DCLK pin 37 <- LCD_CLK (PD18); DEN pin 9 <- LCD_DE (PD19); HSYNC 11 <- PD20; VSYNC 10 <- PD21. LED-A pins 1-2 = Backlight V+, LED-K pins 3-4 = Backlight V-. Panel supply rails 10V3, -6V3, 15V8 from boost converters. Backlight driver PT4103B23F, EN pin = `FPGA_BACKLIGHT_PWM` (FPGA pin 42 on 1013D, 7 on 1014D), so brightness is an FPGA PWM (cmd 0x38).
- FEL: boot ROM enters FEL when no valid boot image. Old-board photo marks two pads "GND HERE" [F picture]; purpose presumably forcing FEL [I].

### 2.2 1013D sheet 2: Data acquisition
- **U3 FPGA**: label "EP4CE6E22C8N or AL3-10" (144-pin PQFP with exposed pad). EP4CE6 = Cyclone IV E, 6272 LE, 276,480 bit RAM, 2 PLL [general knowledge]. New boards use **Anlogic AL3-10** (1014D label "Anlogic AL3-10"). Bitstreams: `Binaries/Original files/FPGA Flash/New version/W25Q80_FPGA.bin` (1 MB, header `CC 55 AA 33 F0 00 00 06`, ~283 KB used, Anlogic format [I]) and `Old version/ST25P16VP.bin` (2 MB, ~118 KB used, bit-reversed Altera-style AS image [I]).
- Config: Active Serial 3V3, config flash **W25Q80** (1 MB), header J2 `FPGA_FLASH`. JTAG pads TDI/TCK/TMS/TDO with 2V5 level (photo `fpga_jtag.jpg`; sheet header J5 on 1014D). Core 1V2, VCCA/PLL 1V2 and 2V5 via 2200 ohm ferrite beads L1-L4.
- **Clock**: 1013D has a fixed 50 MHz oscillator X2 (O705050MEEA4SC) via R4 33 ohm to FPGA CLK pins 23-25. No clock synthesiser. **1014D has MS5351M (U13)** (3-output I2C clock generator, 2.5 kHz-200 MHz, 25 MHz crystal X2): CLK0 via 33 ohm to FPGA pin 24, CLK1 via 33 ohm to pin 23, CLK2 unused; I2C from F1C100s PA0/PA1 (`CLOCK_SDA/SCL`).
- **ADCs: two AD9288BSTZ-100** (dual 8-bit, 100 MSPS per core, TQFP-48). U8 = CH1, U7 = CH2. AINA and AINB of a chip are **tied to the same analog input** (`ANALOGx_ADC_INPUT` via 200 ohm R22/R23). REFINA/REFINB strapped to REFOUT (internal 1.25 V, 1.024 Vpp). DFS grounded (offset binary). S1=S2=high = data-align mode on (per schematic note). ENCA/ENCB separate FPGA outputs.
- **ADC offset** [V/I]: `ADC1_OFFSET` / `ADC2_OFFSET` FPGA outputs go through 2 x 1K and 3 caps to the ADC input node. Looks like a PWM offset DAC injecting DC at the ADC input (fed by cmd 0x32 / 0x35).
- **1013D FPGA pin map** [V]:
  - ADC1 core A: D7A..D0A = 54,55,58,59,60,64,65,66. Core B: D0B..D7B = 67,68,69,70,71,72,73,74. ENCA 75, ENCB 53.
  - ADC2 core A: D7A..D4A = 84,85,86,87; D3A..D0A = 88,89,90,91 (clock-capable pins). Core B: D0B..D7B = 98,99,100,101,103,104,105,106. ENCA 110, ENCB 83.
  - OFFSET: ADC2_OFFSET 111, ADC1_OFFSET 112.
  - Analog controls (1K series): ANALOG1_RELAIS1 121, A1_RELAIS2 124, A2_RELAIS2 125, A1_RELAIS3 126, A2_RELAIS3 127, A2_RELAIS1 128, ANALOG1_AC_DC 129, ANALOG2_AC_DC 132, `1KHZ_CALIB_SIG` 133 (probe-compensation square wave).
  - CPU bus 1,2,3,135-138,141-144. EEPROM SDA 38, SCL 39. BACKLIGHT_PWM 42. Config: ASDO 6, nCE 8, DATA0 13.
  - Unlabelled (spare) IO pins: 7,10,11,28,30-34,43,44,46,49-52,76,77,80,113-115,119,120 [V] (about 24, some are config or clock-capable).
- 1014D map [V]: ADC2 ENCA 64, D7A..D0A 65..72, D0B..D7B 73,74,75,76,77,80,83,84, ENCB 85, ADC2_OFFSET 86, ADC1_OFFSET 87. ADC1: D5A,D6A,D7A,ENCA = 91,90,89,88; D4A..D0A = 98,99,100,101,103; D0B..D7B 110,111,112,113,114,115,119,120; ENCB 121. Relays/AC-DC: 124 A2R1, 125 A2R2, 126 A2R3, 127 A1R3, 128 A1R2, 129 A1R1, 132 A1_AC_DC, 133 A2_AC_DC. **DAC_D0..D7 = 38,39,42,43,44,46,49,50**. CPU bus same as 1013D. No calib pin.

### 2.3 1013D sheet 3: Analog input (two identical channels)
BNC (DOSIN-801-0080) -> **KAQY214STLD photo-MOSFET** shorts the 150 nF AC-coupling cap C99 (control `ANALOGx_AC_DC`) -> R62 150 ohm -> compensated attenuator (150 nF caps C102-105 with R63 3M, R64 2M7, R65 1M2, R68 1M8 to VRF1, dividers R66 75K / R67 360K with 1 nF) -> **HFD4/3-S1R relays** K2 (RELAIS2) and K3 (RELAIS3) select attenuation -> **RS8751XF opamp** (U17/U20, 250 MHz GBW, 2V5 supply) -> K1 (RELAIS1) -> ADC input. Relays driven by SS8550 PNP (Q1-Q6) from 3V3 via 1K. 3 relay bits per channel, 6 codes used. VRF1 reference from TL432 (U21) with 1N5819 diodes. Bandwidth-relevant: input series R, attenuator caps, opamp 250 MHz GBW, 200 ohm ADC series R, AD9288 475 MHz input BW.
1014D analog sheet: same topology, opamp supply 3V3_A, 10 ohm R99/R100 in BNC return, bias RC on VRF1.

### 2.4 1013D sheet 4: Power
- Battery (XH2.54-2P) charged by **two TP4056** (U9, U10) from VUSB, red/green LEDs, BATTERY_CHARGING to PE12.
- S1 power switch, POWER_ENABLE enables **SPX29302T5-L/TR** (3V3 main LDO). TL431 reference.
- AMS1117-1.2 -> 1V2 core. LM6206N3 65Z5 -> 3V, 65T5 -> 2V5. **TPS61040/41** boost -> 15V8, BZT52C5V6 -> 10V3, SS34 + BZT52C8V2 charge pump -> -6V3. PT4103B23F backlight boost from VBAT.
- 1014D: no battery or charger. 5V_IN connector, Q8 CJ3401 P-MOS switch with Q7 SS8050 latch (POWER_HOLD from the GD32E230) and S1 button. 3V3 regulator U9 (marked 6210_2046/33), 3V3_A TLV70033, AMS1117-2.5 x2, AMS1117-1.2, LM6206 3V, same boost chain.

### 2.5 1014D specifics
- No touch panel. PA0..PA3 become `CLOCK_SDA/SCL` (MS5351M I2C) and `CONTROL_RX/TX` (UART to GD32E230).
- **GD32E230C8T6 (U22)** = front-panel MCU: 5x6 key matrix (34 buttons) and 8 quadrature encoders (SELECT, CH1 POS, CH2 POS, TRG POS, TRG LVL, CH1 RNG, CH2 RNG, TRG RNG). UART: PA9 TX / PA10 RX (`CONTROL_TX/RX`) to F1C100s. POWER_HOLD (PA12), POWER_ON/OFF (PA11). SWD header J6. Firmware: `Software reverse engineering/1014D_GD32E230.gar`. 1013D has no such part [F].
- **Signal generator output**: FPGA parallel 8-bit R-2R DAC (1K5/750 ladder) -> RS8751XF buffer U14 (5 V) -> 100 ohm -> BNC RF3 (`ADC_DAC` sheet).
- Same AD9288-100 ADCs and same FPGA as 1013D.
- RS8754XQ datasheet in `IC's` is the quad member of the RS875x opamp family; only RS8751XF appears in the BOM.

### 2.6 BOM highlights (EasyEDA JSON)
F1C100s; W25Q32FVSSIGTR (CPU flash and FPGA flash footprint); X503224MSD2GC 24 MHz; O705050MEEA4SC 50 MHz; USB-310D-B-SU; HY-TF1007F; 24LC04BT-I/SN; 2 x AD9288BSTZ-100; 6 x HFD4/3-S1R; 2 x RS8751XF; 6 x SS8550; 2 x KAQY214STLD; 2 x TP4056; SPX29302T5-L/TR; AMS1117-1.2; 2 x LM6206N3; TPS61040DBVR; PT4103B23F; 4 x SS34; BZT52C5V6; BZT52C8V2; 4 x BLM21PG221SN1D; 2 x SMDRI74-101MT 10 uH.

---------------------------------------------------------------------
## 3. Derived analysis

### 3.1 Sample rate and interleave
- AD9288 core: 100 MSPS, 8 bit, min 1 MSPS. Two cores per channel on one input, independent encode clocks, data-align on. **Max 200 MSa/s per channel, both channels at once, 8 bit** [I from schematic; consistent with new fw table where 200 MSa/s is the top rate].
- Streams: core A via 0x20/0x22, core B via 0x21/0x23. New fw interleaves them (ADC1 -> buffer[1], ADC2 -> buffer[0], stride 2) with per-core gain/offset compensation: 1500 + 1500 = 3000 samples per channel [F fpga_control.c, scope_functions.c nofsamples=1500, samplecount=3000].
- New-fw rate command (author's hardware-tested): `0x0D` W4 = N, fs = 200 MSa/s / (N+1) [F variables.c `sample_rate_settings`]:
  N = 0,1,3,9,19,39,99,199,399,999,1999,3999,9999,19999,39999,99999,199999,399999 gives 200M, 100M, 50M, 20M, 10M, 5M, 2M, 1M, 500k, 200k, 100k, 50k, 20k, 10k, 5k, 2k, 1k, 500 Sa/s.
  Stock values 0x27 (= 39, 5 MSa/s) and 0x7D0 (= 2000, about 100 kSa/s) in roll mode fit this meaning [I].
- New-fw timebase mapping (`time_per_div_sample_rate`, 24 steps 200 ms..5 ns): 500 ns/div and faster all use 200 MSa/s; each slower step maps to the next rate down. (Stock steps 10 ns / 25 ns / 50 ns... at 200 MSa/s are screen magnification of the same data plus repeated shots [I].)
- Stock actual sample rates per timebase are not stated in the repo; only the 0x0E/0x1F parameters.

### 3.2 Memory depth
- Pointer returned by cmd 0x14 is 12 bits, start address wraps mod 4096 => **circular buffer 4096 samples per ADC core** [I]. Firmware reads 1500 per core (750 at 20/50 ms), trigger fixed at index 750. That is 4 cores x 4096 x 8 bit = 128 Kbit of the EP4CE6's 276 Kbit.
- With the existing bitstream a host could read up to 4096 per core (8192 per channel interleaved) by reading more bytes [I, untested]. More needs a new bitstream (no FPGA source in the repo, only binary images).
- No external RAM on any sheet.

### 3.3 Limits
- Depth: FPGA block RAM and the fixed bitstream address width.
- Rate: 2 x 100 MSPS ADC; clock from fixed 50 MHz osc plus FPGA PLL (1013D) or programmable MS5351M (1014D, up to 200 MHz). Analog front-end limits (150 ohm, attenuator caps, 250 MHz opamp) set real bandwidth.
- CPU link: bit-banged 8-bit bus, 2 GPIO writes per byte.
- Trigger: one 8-bit level, source, edge, auto/normal only; no pulse or hold-off commands known.

### 3.4 Unused capability
- About 24 spare FPGA IO pins (2.2). `1KHZ_CALIB_SIG` output exists with no command identified. AD9288 standby pins and DFS are hard-wired. 1014D DAC command interface not documented here (see `Test code/1014D_*`). Unknown commands 0x0E, 0x1F (stock), 0x3C, 0x17 stock constants.

---------------------------------------------------------------------
## 4. ICs and manuals
| File | Part | Role / key spec |
|---|---|---|
| IC's/AD9288.pdf | AD9288BSTZ-100 | Dual 8-bit ADC, 100 MSPS/core, 475 MHz BW, 1.024 Vpp, 3 V, 4 pipeline delays, S1/S2 modes (11 = normal; align mode delays B 1/2 clock), DFS low = offset binary |
| IC's/F1C100s_Datasheet_V1.0.pdf, Manuals/F1C100s_Datasheet_V1.0 NC.pdf | Allwinner F1C100s | ARM926EJ-S, SiP DDR1, boot from SPI NOR/NAND or SD |
| Manuals/Allwinner_F1C200s_User_Manual_V1.2.pdf | F1C200s manual | Register-level manual for the F1C100s family |
| Manuals/musbmhdrc_pspgUSB.pdf | Mentor MUSBMHDRC guide | USB OTG controller used in F1C100s |
| Manuals/Cyclone IV/ (cyc_c51013, cyclone4-handbook) | Cyclone IV | FPGA family reference |
| Manuals/GD32E230xx_Datasheet, GD32E23x_User_Manual | GD32E230C8T6 | 1014D front-panel MCU |
| Manuals/Nano F600 User Manual NC.pdf | Lichee Nano | Dev board, same SoC |
| Manuals/ARM_926EJS_TRM, ARM architecture ref, ARM_inst_ref_card | ARM docs | |
| Manuals/SCSI_commands_reference, usbmassbulk_10 | USB mass storage | scope USB disk mode |
| IC's/GT911-GOODIX, GT911 Programming Guide_v0.1 | GT911 | touch, I2C 0x5D/0x14, point 1 X reg 0x8150 |
| IC's/MS5351M, Si5351A_B_C | MS5351M / Si5351 | I2C clock generator (1014D) |
| IC's/50MHz_XOSC.pdf | SMD crystal sheet (4.0x2.5 mm) | clock source datasheet |
| IC's/W25Q16_Winbond, Winbond-w25q32 | W25Q16 2 MB, W25Q32 4 MB | CPU flash (W25Q80 for FPGA) |
| IC's/...RS8754XQ... | RS8751/2/4 | 250 MHz GBW rail-to-rail opamp |
| IC's/LM6206N3, HFD4, KAQY214, tp4056, tps61041, PT4103, AMS1117, SPX29302T5, tl431, ss32, ss8550, CJ3401, Y1-Q200 psu | support parts | regulators, relays, power |
| IC's/lcd_pinout.png | 50-pin RGB LCD | see 2.1 |

---------------------------------------------------------------------
## 5. Pictures
- Old version: `Main_board_old_system_large.jpg` (16.9 MB), `cpu old system.png`. Legible: "F1C100S KA0458A 28L2", 24 MHz crystal, Winbond SOP8 beside 2x3 header, TF slot, annotation "GND HERE" at two pads. Older board uses ST25P16VP FPGA flash [I].
- New version: `fpga and adc's.jpg` (QFP-144 FPGA plus two TQFP-48 ADCs, shields over analog), `fpga_jtag.jpg` (pads TDI, TCK, TMS, TDO, GND, 2V5; chip top mark 018191043190), `Back side of PCB.jpg` (BNCs, battery, 2-layer-looking routing), `Analog section new version.jpg`, `Opamp in analog section.JPG`, `Power regulators.jpg`, `Reset pullup.JPG` (not viewed in detail).

---------------------------------------------------------------------
## 6. Backup, sunxi, shopping list
- `Original firmware backup/fnirsi_1013d_fwb.bin` (180,296 B): SD image written with `dd bs=1024 seek=8`. On boot it dumps FNIRSI_1013D_full_flash_backup.bin, FNIRSI_1013D_tp_config.bin and FWB_FSI_1013.bin (rename to FNI-1013.bin to restore; works only on newer bootloader). Warning: 0x1F4 bytes of calibration data at flash 0x001FD000; 0x001FE000 = 0x9086 skips hw check.
- Flash pieces (`Binaries/Separated parts`): SPL 13,279 B; 1st exec 31,199 B; bitmaps 58,839 B; 2nd exec 1,653,215 B. Full images W25Q32.bin 4 MB, W25Q16 2 MB.
- `sunxi_stuff/`: `sunxi-fel` (SPI-flash build) and `fel-sdboot.sunxi` (8 KB). Make FEL SD: `dd if=fel-sdboot.sunxi of=/dev/sdX bs=1024 seek=8`. Read: `sunxi-fel -p spiflash-read 0 2097152 out.bin`. Write: `-p spiflash-write 0 in.bin`. Dev flow: `sunxi-fel -p spl fnirsi_1013d_startup_with_fel.bin` then `-p write 0x7FFFFFE0 fnirsi_1013d_scope.bin exe 0x80000000` (loads with DRAM enabled and FEL active). SD run flow: `Binaries/New software/readme.txt`.
- Shopping list (`Aliexpress links to handy stuff`): 7" display, type-B 164x99 mm touch panel (GT9157 possible), W25Q32, CH341 programmer, FT232 serial, SOP8 socket, FFC-to-header boards (6p touch, 50p LCD), Lichee Nano, ST-Link V2, STM32F103 Bluepill.

## 7. Gaps for the MCP/PC-control work
- All FPGA commands needed (range, coupling, offset, trigger, sample rate, arm, read) are in 1.3 and callable without the special IC.
- Undecoded: 0x0E meaning, 0x1F stock values, 0x3C, 0x17 stock constants, calibration flash layout, 1KHz output, 1014D DAC and GD32 UART protocol.
