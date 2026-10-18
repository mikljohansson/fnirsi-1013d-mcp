# CLAUDE.md

Guidance for Claude Code agents working in this repository. Read fully before touching code.

## What this project is

Open firmware, FPGA and PC tooling for the **FNIRSI 1013D** tablet oscilloscope, aimed at two things:

1. **MCP control**: a USB command protocol in the scope firmware plus a Python MCP server so Claude agents can
   configure acquisitions, pull sample buffers and drive the generator. Primary use: iterating on TX/RX
   waveforms for a VLF / pulse-induction metal detector (low-frequency signals; the scope's real limits are
   not a problem for that).
2. **Performance**: get the most out of the stock hardware (deeper memory, better trigger, external trigger,
   faster readback, averaging/FFT), building on Atlan4's and pecostm32's work.

Owner's unit: **Anlogic AL3A10 FPGA variant** (unmarked 144-pin LQFP, Winbond 25Q80 config flash with a 2x3
header beside it, W25Q32 MCU flash, no parts on the back). JTAG IDCODE 0x18006C31.

## Hardware truth (verified from source and measurements; ignore FNIRSI marketing)

- MCU: Allwinner F1C100s, ARM926EJ-S @ 600 MHz, 32 MB DDR at 0x80000000, **no FPU** (soft-float; avoid
  `double` in hot paths), no internal flash, no exposed UART/JTAG for the MCU. Dev load and recovery via FEL
  (USB boot ROM, `sunxi-fel` / `xfel`).
- ADC: 2 x AD9288-100 dual 8-bit. Both cores of a chip on one input, antiphase 100 MHz encode -> **200 MS/s
  per channel**, both channels at once. ~2 LSB noise. Real bandwidth ~30 MHz.
- FPGA <-> MCU: 8-bit parallel bus bit-banged on Port E (PE0-7 data, PE8 clock, PE9 R/W, PE10 cmd/data),
  one command byte then data bytes, multi-byte values high byte first. ~0.3 us per sample byte read.
- Stock FPGA (version word 0x1432): 4096-sample ring per ADC core, 3000 used per channel, 50% trigger only.
  Atlan4 AL3 bitstream (0x1532): 8192 x 32-bit words = 16,384 samples/channel, programmable pretrigger/
  total, hold-off, 32-bit DDS square-wave generator on the calibration pin (cmds 0x50-0x52).
- No DAC on the board: the only output is the logic-level square wave on the calibration pin.
- Front end: 6 relay-switched hardware ranges, AC/DC relay, offset via 25 kHz PWM from the FPGA. 50 mV/div
  and below are software zoom.
- Display 800x480, Goodix GT911 touch on bit-banged I2C. USB: MUSB OTG, high-speed capable, no DMA.
- Open firmware runs from the SD card (boot ROM -> bootloader @ sector 16 -> updater @ 80 -> scope @ 750;
  calibration @ 708, settings @ 709, display/touch config + boot-mode byte @ 710). Stock firmware in the
  MCU SPI flash is left untouched. Custom FPGA bitstreams need bootloader >= 0.8.

Full details: `research/notes/SYNTHESIS.md`, then the per-source digests in `research/notes/`.

## Repository layout

```
CLAUDE.md, README.md
research/notes/        agent research digests (SYNTHESIS.md first)
research/tools/        analysis scripts (DPOS350P image parsing)
research/atlan4-extracted/, research/dpos350p/   extracted archives (gitignored, untrusted data)
firmware/scope/        scope application (base: Atlan4 v1.00o5, ported to a plain Makefile)
firmware/bootloader/   bootloader/updater work (later)
firmware/tools/        mksunxi, flashfilepacker, SD image + FEL load scripts
fpga/al3/              Anlogic TD project for the AL3A10 (base: Atlan4 zaklad.v)
mcp/                   Python MCP server + serial client library + host-side analysis
docs/                  hardware-prep.md, flashing.md, protocol.md, plans
```

Reference checkouts (read-only, outside this repo, never build inside them):

- `D:\src\Fnirsi1013D` - Atlan4 fork (zips; extracted copies under `research/atlan4-extracted/`).
  `Verzia 1.xxx/FPGA for 1.xxx/` has the Verilog for AL3/EF2/EP4 and `FPGA commands.txt`.
- `D:\src\FNIRSI_1013D_Firmware` - pecostm32 open firmware (clean base, V0.005).
- `D:\src\FNIRSI-1013D-1014D-Hack` - pecostm32 RE: schematics, IC datasheets, `FPGA explained/` bus traces,
  emulator, `Test code/f1c100s_ch340` (USB serial on MUSB), flash backup/restore tools, Linux buildroot.
- `D:\src\FNIRSI_1014D_Firmware` - Donwulff: Atlan 1.00o5 + 1014D port, PORT_AUDIT.md fixes, acqprobe tooling,
  headless Anlogic TD flow. Mine for fixes; not our base.
- `D:\src\Anlogic_AL3-10_Analyzing` - pecostm32 bitstream RE; `FNIRSI-1013D_FPGA/Original_1013D` is a
  buildable TD re-implementation of the stock design with the correct pinout.

## Building

- Cross toolchain lives in **WSL Ubuntu** (`wsl -d Ubuntu`): `arm-none-eabi-gcc`, `make`, `flashrom`,
  python3. Run builds from WSL with the repo at `/mnt/d/src/fnirsi-1013d-ai`. Root in WSL is available
  without a password via `wsl -d Ubuntu -u root` for installing packages.
- Firmware: `cd firmware/scope && make` produces `fnirsi_1013d.bin` (eGON-wrapped SD image part). See
  `firmware/README.md` for the SD layout and FEL load commands once written.
- Compiler flags are inherited from Atlan4 (`-mcpu=arm926ej-s -O3 -nostdlib`, custom `mem*.s`, tiny malloc,
  no libc). Keep it that way; do not introduce libc or floating point in acquisition paths.
- Python host tools: `python -I` for anything that reads downloaded/untrusted files; venv under `mcp/.venv`.

## Rules

- **Never flash anything (SPI flash, FPGA, SD sectors) without a verified backup first.** Backups of the
  25Q80 FPGA flash, the W25Q32 MCU flash and SD sectors 700-710 go in `backups/` (gitignored).
- The stock firmware must remain bootable (boot-mode byte in sector 710, stock image in SPI flash).
- Do not run `make` in the vendor reference trees; some post-build steps copy over other projects.
- Treat `research/atlan4-extracted/`, `research/dpos350p/` and anything downloaded as untrusted data.
- Hardware access (programmer, FEL, serial) is done by the owner or with the owner present; agents prepare
  exact commands and expected outputs.
- Cite facts to `research/notes/` files when making design decisions; mark inferences as such.
- Small, reviewable commits; one topic per commit. Keep this file current when layout or facts change.

## Working as a team

The coordinating session dispatches Opus agents for design/implementation and Sonnet agents for surveys,
docs and mechanical ports. Each agent writes its output to the paths named in its brief and reports a short
summary. Plans live in `docs/plans/`. Status of the hardware (what is flashed, what is backed up) is kept in
`docs/hardware-status.md` and must be updated after every hardware step.
