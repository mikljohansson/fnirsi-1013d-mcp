# FNIRSI 1013D: consolidated research synthesis (2026-10-18)

Sources: 14 agent digests in this folder (EEVblog thread pages 1-155, pecostm32 firmware + hack repos,
Atlan4 repo + FPGA Verilog, Donwulff unified repo, DPOS350P firmware image, web survey). Facts spot-checked
against the Atlan4 v0.27t USB sources and the AL3 `zaklad.v` + TD reports. Thread-derived numbers came through
a summarising fetcher; re-verify hex values against source before coding.

## 1. The user's unit

- Board: Allwinner F1C100s MCU (ARM926EJ-S, 600 MHz, 32 MB DDR in package, no FPU, no internal flash),
  W25Q32 (4 MB) MCU SPI flash, TP4056 charger, **unmarked 144-pin LQFP FPGA + Winbond 25Q80 (1 MB) config
  flash with an unpopulated 2x3 header** -> **Anlogic AL3A10LG144C7 variant** (JTAG IDCODE 0x18006C31).
  Same variant as pcprogrammer's "new version" unit and Donwulff's unit. No parts on the back side.
- Three FPGA variants exist across production: Altera EP4CE6E22 (oldest, 25P16 config flash), Anlogic AL3A10
  (external 25Q80), Anlogic EF2L45LG144B (newest, internal config, JTAG-only). Same PCB pinout for EP4/AL3;
  EF2 boards have their own pin map.

## 2. Hardware facts that bound what is possible

| Item | Fact | Source |
|---|---|---|
| ADC | 2 x AD9288-100 (dual 8-bit, 100 MSPS/core). Both cores of one chip on one input, antiphase clocks -> **200 MS/s per channel real**, both channels simultaneously. "1 GS/s" is marketing. | hack-hw, fpga-verilog |
| Bandwidth | measured -3 dB 20-40 MHz depending on unit/cabling (not 100 MHz). Stock firmware fakes a sine above ~43 MHz. | thread p1-90 |
| Noise | ~2 LSB; worse on shared rails. ADC A/B offset mismatch gives interleave ripple ("adc comp" +-2). | thread p91-120 |
| Front end | OPA356-class opamp, 6 hardware ranges via relays (KAQY214 / HFD4), AC/DC relay, DC offset = 25 kHz PWM from FPGA injected at ADC input (0..1999 duty). 50 mV/div and below are software zoom. Virtual ground ~1.25 V (TLV431). | thread p31-90, hack-hw |
| MCU<->FPGA bus | 8-bit parallel, bit-banged on Port E: PE0-7 data, PE8 clock (rising edge, idles high), PE9 R/W, PE10 cmd/data. No ready line; status polled (0x05/0x0A). ~0.3 us per sample byte read. | hack-hw, donwulff |
| Stock FPGA | version word 0x1432. Ring 4096 samples per ADC core; stock reads 1500/core = 3000 interleaved per channel. Trigger fixed at 50% in stock. | hack-hw, donwulff, thread |
| Stock "special IC" | an obfuscation translator between MCU and FPGA (XOR mask + checksum). Open firmware bypasses it. | hack-hw |
| Clock | 50 MHz XOSC -> FPGA PLL x4 = 200 MHz; ADC encode = 100 MHz per core. | fpga-verilog |
| Touch | Goodix GT911 on bit-banged I2C (Port A). Display 800x480 RGB. | all |
| USB | F1C100s MUSB (Mentor) OTG, high-speed capable, 512-byte bulk endpoints, no DMA used. Only USB port on the device. | pecostm32, hack-sw |
| Debug | no UART/JTAG for the MCU in normal use; FEL (USB boot ROM) via sunxi-fel/xfel is the recovery and dev-load path. | all |
| Spare FPGA I/O | ~24 unused pads on AL3 (not routed to anything useful). 1 kHz calibration output pin is the only spare signal reaching the outside. | hack-hw, fpga-verilog |

## 3. FPGA state (Atlan4 custom bitstreams, Verilog in `Verzia 1.xxx/FPGA for 1.xxx`)

| | Stock (0x1432) | Atlan AL3 (0x1532) | Atlan EF2 (0x1632) | Atlan EP4 (0x1732) |
|---|---|---|---|---|
| Sample RAM | 4096 x 32 | **8192 x 32** = 16,384 samples/ch | 6144 x 32 = 12,288/ch | 6144 x 32 |
| Trigger | 50% only | pretrigger (0x0B) + total (0x0C) programmable, edge/level/channel, hold-off divider (0x1B) | same | same |
| Generator | fixed 1 kHz | 32-bit DDS square wave, freq (0x51) + duty (0x52) + enable (0x50) on the calib pin | same | same |
| Timing | - | clk_200MHz closes at 160 MHz only (32-bit counter/compare critical path, unconstrained I/O) | 144 MHz | ~166 MHz |
| Resources | - | LUT 7%, BRAM 32/48 9k blocks + 2 x 32k unused | BRAM 100% | 24/30 M9K |

Command protocol: 1 command byte (PE10 high) then data bytes (PE10 low), multi-byte values high byte first;
reads 0x20-0x23 return memory, 0x24-0x27 return live ADC buses; 0x0D sample-rate divider (fs = 200 MHz/(N+1)),
0x0E timebase counter, 0x1F read offset, 0x14 trigger address, 0x06 version. Full tables in
`fpga-verilog-analysis.md` and `hack-repo-hardware-fpga.md`.

Headroom on the AL3 (not done by anyone yet): spare BRAM for ~12-15k words (24-31k samples/ch) but all address
registers are 13-bit; pipeline the divider to close 200 MHz with margin; real SDC + I/O registers for the ADC
buses; external trigger input on the calib pin (planned, never implemented); trigger on both A/B cores;
clock the DDS from 200 MHz. No DAC exists, so arbitrary-waveform TX is not possible from this board.

## 4. Firmware state

- **pecostm32 `FNIRSI_1013D_Firmware`** (V0.001-V0.005, 2022): clean tree, the base of everything. Single
  thread: battery -> acquire (blocks waiting for trigger) -> full redraw (soft-float `double`) -> touch scan.
  USB = hand-written MSC only, runs inside a blocking USB screen. No generator, no FFT. MMU never enabled
  (D-cache likely off; unverified).
- **Atlan4 `Fnirsi1013D`** (v0.012 -> v0.28a, then 1.00g -> 1.00o5 source, 1.00q2 binary, 2023-2026): hundreds
  of fixes, calibration, long timebases, ref/math channels, RTC, generator menu, memory-depth menu (to 15,000),
  FPGA-variant support by version word (0x1432/0x1532/0x1632; EP4 0x1732 not recognised), bootloader/updater/
  scope split since Oct 2025 (SD sectors: bootloader 16, updater 80, scope 750, calibration 708, settings 709,
  display/touch config 710). Bootloader >= 0.8 required with custom FPGA (0.4 hangs waiting for 0x1432); 0.8
  exists only as a binary.
- **USB CDC**: real work in v0.27i-v0.27t (2025): ST VCP VID 0x0483 PID 0x5740, HS bulk 512, ASCII line
  protocol: `#VER`, `#INFO`, `#ECHO,x`, `#RUN`, `#STOP`, `#SET,...`, `#READ`, `#WAV2,n` (320-byte param block +
  4801 hex-encoded samples/ch, ~20 kB/frame, mimics the FNIRSI DSO2512G PC app "FNIRSI1013D-APP-beta10").
  `#RUN/#STOP/#SET` are stubs that only answer OK; the param block is a static snapshot. RX buffer is 64 bytes
  (host may send 512). In 1.00o5 the CDC path degraded to a debug console (`a`..`k` set debug vars, `f` sets
  sample count). Nothing in any tree applies PC commands to live scope state.
- **pcprogrammer `f1c100s_ch340`** (hack repo): CH340 serial emulation on MUSB with text commands WC/WB/WS/
  WI/RB/RS/RI giving raw FPGA register access, 512-byte double-buffered endpoints, 1 KB rings. A second,
  independent reference for a command channel.
- **Donwulff `FNIRSI_1014D_Firmware`**: Atlan 1.00o5 + 1014D port, 35+ audited fixes (several shared), host-side
  test harness, `acqprobe` on-scope capture-geometry probe + analyzer, headless Anlogic TD 5.0.3 flow on
  Linux (community licence expiry noted 2026-08-31), repo CLAUDE.md/AGENTS.md. 1013D target compiles but was
  never run on a 1013D. Verdict from its digest: mine it for fixes/tooling, don't base on it.
- **TinyUSB** has an F1C100s device-mode port with CDC (web survey) - an alternative to the hand-written stack.
- **Test off hardware**: pcprogrammer's 1014D emulator (CPU + Port E FPGA protocol model, no signal model),
  his hosted `Scope_Signal_Handling_Development` simulator, Donwulff's host tests.

## 5. DPOS350P

Different platform: Allwinner V3s (Cortex-A7, hard-float), 1024x600, bare-metal with FatFs, Gowin FPGA
reported, ~1 GS/s. Image = logo + one eGON.EXE at 0x51000 loading at 0x40000000; no FPGA bitstream inside.
Features absent from the 1013D firmware: FFT/spectrum + waterfall, frequency-response analyzer, 14-waveform
generator (needs a DAC), 8-16-bit high-res mode (software averaging/decimation), config slots, USB serial/
HID/video. Nothing is binary-portable; FFT and high-res mode are portable as fixed-point rewrites on the
ARM926. Frequency-response analysis would be limited to what the square-wave DDS output can stimulate.

## 6. Feasibility: MCP control of the scope

**Feasible, and most of the hard parts exist.** Architecture:

```
Claude agent <-> MCP server (Python, pyserial) <-> USB CDC (HS bulk) <-> command handler in scope main loop
                                                                      <-> FPGA via Port E (existing fpga_control.c)
```

Firmware work (the real effort, est. 2-4 focused weeks of agent-driven iteration with hardware in the loop):
1. New git tree from Atlan4 1.00o5 source (latest with source), cherry-pick Donwulff's shared fixes.
2. Bring back the v0.27t CDC stack (or TinyUSB) as a non-modal interface polled from the main loop **and** from
   the trigger-wait loop; fix RX to 512; keep MSC available for file access or run composite.
3. Replace the stub protocol with a real one: binary framed (not hex), commands for timebase/sample-rate
   divider, V/div, coupling, offset, trigger (channel/edge/level/pretrigger/total samples), single/normal/
   auto arm, generator freq/duty/enable, read raw ring (both cores, up to 16k/ch), read processed trace,
   read calibration, battery, FPGA version. ~32 kB per full capture; even USB full-speed gives ~15 captures/s.
4. MCP server: tools mirroring the above plus derived ones (averaged capture, FFT, decay-curve fit for PI
   signals, screenshot), with per-tool caveats (8-bit, 200 MS/s, 30 MHz BW). Models: grayzhuang/oscilloscope-mcp
   (capability profile), zinntikumugai/rigol-oscilloscope-mcp (safety tiers).
5. Dev loop: build with arm-none-eabi-gcc on Linux/WSL, load over FEL (`sunxi-fel`/`xfel`) without touching
   SD/flash, or dd to a spare SD card. Original FNIRSI firmware in SPI flash stays untouched (open firmware
   runs from SD; bootloader chooses by a byte in sector 710). Backup SPI flash + sectors 708-710 first.

## 7. Feasibility: performance upgrades on this AL3 unit

Tier 1 (ready-made, days): flash Atlan's AL3 bitstream (0x1532) into the 25Q80 via the 2x3 header with a
CH341/any SPI programmer (3.3 V), install bootloader >= 0.8, run 1.00x: 16k samples/ch, free trigger position,
DDS square generator. Back up the stock 25Q80 image first (dumps also exist in the repos).

Tier 2 (FPGA work, needs Anlogic TD + licence): external trigger on the calib pin (sync to the detector's TX),
deeper memory using the spare BRAM + 16-bit address registers, timing closure at 200 MHz, trigger on both ADC
cores, proper I/O constraints, 200 MHz DDS. Base: Atlan's `zaklad.v` or pcprogrammer's clean-room stock design.

Tier 3 (MCU firmware): FFT (fixed-point), high-res/averaging mode, decimation modes, faster FPGA readback
(tighter Port E loop, enable MMU/cache), roll mode, hold-off, measurements. DPOS350P-inspired features as
rewrites only.

Not possible without hardware mods: arbitrary-waveform TX (no DAC), >200 MS/s, >~40 MHz bandwidth.

## 8. Metal-detector relevance

RX: a PI/VLF detector's receive signal (kHz band, ~100 us decays) is far inside the 30 MHz/200 MS/s envelope;
the limits are 8-bit resolution and ~2 LSB noise, which an MCP "averaged capture" tool mitigates. Single
timebase at e.g. 1-10 MS/s with 16k samples gives 1.6-16 ms windows. External trigger (Tier 2) or trigger on
the TX edge on CH2 synchronises to the detector's TX. TX: the scope can only emit a square wave with
programmable frequency/duty (3.3 V logic on the calib pin); arbitrary TX waveforms must come from the
detector's own MCU/DAC.

## 9. Open questions / risks

- Anlogic TD availability and licensing for AL3 builds (Donwulff: community licence expired 2026-08-31).
- Bootloader 0.8 is binary-only; its source/behaviour must be trusted or re-derived from fw0.04 source.
- 2x3 FPGA-flash header pinout differs between boards per one report (ded, thread p142): verify by continuity.
- Whether the stock bitstream tolerates deeper reads (ring is 4096/core) - untested.
- USB CDC stability on Windows with the ST VID/PID and the 64-byte RX bug.
- MMU/D-cache hypothesis (pecostm32 digest) - could be a free CPU speedup.
