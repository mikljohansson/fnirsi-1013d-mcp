# AGENTS.md

Repository: **fnirsi-1013d-mcp** (local checkout directory may still be named `fnirsi-1013d-ai`).

Guidance for AI agents (Claude Code, Codex, others) and humans working in this repository. Claude Code reads this
file when no `CLAUDE.md` exists. Subdirectories carry their own `AGENTS.md` handoffs: `firmware/AGENTS.md`,
`mcp/AGENTS.md`, `fpga/al3/AGENTS.md`.

## If a person asked you to help install or use the scope

- **Installing the firmware, the card or the FPGA bitstream**: follow `docs/guide/agent-runbook.md`. It has the
  questions to ask first, the safety gates you may never skip, the commands in order and the recovery paths.
- **Setting up the MCP server** so you can use the scope over USB: `docs/agent-setup.md`.
- **Measuring with the scope**: `docs/agent-measurement-guide.md` (Claude Code loads it as the skill
  `.claude/skills/fnirsi-scope/`); the tools and their safety tiers are in `docs/mcp-tools.md`.

The rest of this file is for developing the firmware, the FPGA design and the host tools. Read it fully before
touching code.

## What this project is

Open firmware, FPGA and PC tooling for the **FNIRSI 1013D** tablet oscilloscope, aimed at two things:

1. **MCP control**: a USB command protocol in the scope firmware plus a Python MCP server so Claude agents can
   configure acquisitions, pull sample buffers and drive the generator. Primary use: iterating on TX/RX
   waveforms for a VLF / pulse-induction metal detector (low-frequency signals; the scope's real limits are
   not a problem for that).
2. **Performance**: get the most out of the stock hardware (deeper memory, better trigger, external trigger,
   faster readback, averaging/FFT), building on Atlan4's and pecostm32's work.

The verified hardware is the **Anlogic AL3A10 FPGA variant** (unmarked 144-pin LQFP, Winbond 25Q80 config flash
with a 2x3 header beside it, W25Q32 MCU flash, no parts on the back). JTAG IDCODE 0x18006C31.

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
AGENTS.md (project rules; Claude Code reads it when no CLAUDE.md exists), README.md
research/notes/        agent research digests (SYNTHESIS.md first)
research/tools/        analysis scripts (DPOS350P image parsing)
research/atlan4-extracted/, research/dpos350p/   extracted archives (gitignored, untrusted data)
firmware/scope/        scope application (base: Atlan4 v1.00o5, ported to a plain Makefile)
firmware/bootloader/   bootloader/updater work (later)
firmware/tools/        mksunxi, flashfilepacker, SD image + FEL load scripts
fpga/al3/              Anlogic TD project for the AL3A10 (base: Atlan4 zaklad.v); dist/ holds the released
                       bitstreams with manifest.json and BITSTREAMS.md (checked by tools/bitstreams.py)
mcp/                   Python MCP server + serial client library + host-side analysis
docs/                  hardware-prep.md, protocol.md, mcp-tools.md, agent-measurement-guide.md, releasing.md,
                       development.md, fpga-flash-from-mcu.md, hardware-status.md (bench log), guide/ (user
                       guide, agent-runbook.md), plans/ (task-queue.md first), release-notes/ (user-facing
                       notes per release)
```

Reference checkouts of the upstream projects (Atlan4, pecostm32, Donwulff) are read-only and live outside this
repository; the maintainer's paths are in the last section. Never build inside them.

## Building

- Cross toolchain lives in **WSL Ubuntu** (or any Linux): `arm-none-eabi-gcc`, `make`, `flashrom`,
  python3. Run builds from WSL with the repo mounted under `/mnt/`.
- Firmware: `cd firmware/scope && make -j` produces `build/fnirsi_1013d.bin` (eGON.EXE image for SD sector
  750 / FEL). apt toolchain is GCC 13.2.1; release and bench images use GCC 10.3.1 (`firmware/tools/fetch_gcc10.sh`,
  `make gcc10` gives `build-gcc10-pc/`; `make gcc10-nommu` builds the same code with the MMU off, version suffix
  `-nommu`, for the fw 0.2.48 A/B). The `PC_PROTOCOL=0` build and the byte-identical check against Atlan's v1.00o5
  binary are history: the owner dropped that rule on 2026-10-18 and fw 0.2.47 removed them (lineage: README "Credits",
  git history, CI logs up to that date). Images are reproducible: they depend only on the source, the toolchain and
  the commit (identity = commit hash and commit time read from git), so the same commit gives the same sha256 in any
  checkout and in CI (`firmware/README.md` "Reproducible images", check in `docs/releasing.md`). SD layout, image
  format, `make_sd_image.py` and FEL commands: `firmware/README.md`; SD packages for a release:
  `tools/make_release_sd_package.py`. The developer overview, with the mcp and FPGA builds: `docs/development.md`.
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
- Bench coordination: the scope's serial port is exclusive (the maintainer's port names are in the last section).
  Only the coordinating session takes REMOTE or sends hardware-changing commands; agents may use
  PING/TEST_PATTERN/GET_* in LOCAL and must close the port when asked. Expect E_BUSY whenever the owner is in a
  scope menu (every menu is a modal loop).
- Dev loop: `make gcc10` in firmware/scope, boot menu -> FEL (or ENTER_FEL once available), attach the FEL
  device to WSL, `sunxi-fel -p write 0x7FFFFFE0 build-gcc10-pc/fnirsi_1013d.bin exe 0x80000000`
  (`fnirsi1013d-flash` does all of it; `docs/development.md`). The SD card image is only rewritten for releases.
- UI work (menus, views, dialogs, the bars) follows `docs/ui-guidelines.md`; the current change list is
  `docs/plans/ux-makeover.md` and its mockups are the acceptance reference.
- Shared `main`: update with `git fetch && git merge --ff-only origin/main` (the repo config sets `pull.ff=only`); never
  rebase main (it carries merge commits from agent branches); agents push only their own commits unless the coordinator says so.
- Small, reviewable commits; one topic per commit. **Push once per effort** (a feature, a bugfix, a merge with its bench
  entries, a batch of screenshots), not after every commit, and open one pull request per effort: every push to main and
  every PR update runs the full CI workflow, which costs the owner's GitHub Actions minutes (owner, 2026-10-20). Accumulate
  commits locally and push when the effort is done, or when another agent needs them on `origin/main`. Keep this file and the subdirectory `AGENTS.md` files current when layout or facts change. Agents with very
  large contexts are rotated: before retiring, an agent updates the `AGENTS.md` of the directory it worked in.

## Using the scope from an agent

Measurement sessions through the `fnirsi1013d` MCP server (setup: `docs/agent-setup.md` and `mcp/README.md`, also
for Codex): follow `docs/agent-measurement-guide.md`. Claude Code loads it as the skill `.claude/skills/fnirsi-scope/`.

## CI and releases

GitHub Actions run `.github/workflows/ci.yml` on every push and pull request to main. It covers
the GCC 10, GCC 13 and PC_DEBUG builds, host tests, the
mcp ruff and pytest runs on Linux and Windows, and both protocol-vector checkers. Keep those commands
green locally before pushing. A `vX.Y.Z` tag runs `release.yml`. The tag must equal `PC_FW_VERSION`
and the mcp version, `CHANGELOG.md` needs a matching section and `docs/release-notes/X.Y.Z.md` holds the
release notes; a `vX.Y.Z-rcN` tag is a dry run. Releases attach the bitstreams in `fpga/al3/dist`. Atlan4's binary-only bootloader and
updater are never attached; `tools/make_release_sd_package.py` fetches them on the user's machine.
Procedure, FEL hash check and the GPL reasoning: `docs/releasing.md`. Add user-visible changes to
`CHANGELOG.md` under Unreleased, citing the commit.

## Working as a team

The coordinating session dispatches Opus agents for design/implementation and Sonnet agents for surveys,
docs and mechanical ports. Each agent writes its output to the paths named in its brief and reports a short
summary. Plans live in `docs/plans/`. Status of the hardware (what is flashed, what is backed up) is kept in
`docs/hardware-status.md` and must be updated after every hardware step.

## The maintainer's bench

Facts specific to the maintainer's machine and unit. Another contributor substitutes their own.

- **The unit**: the owner's FNIRSI 1013D is the Anlogic AL3A10 variant described above; its bench history is
  `docs/hardware-status.md`.
- **Serial port**: COM10 on Windows, `/dev/ttyACM0` when attached to WSL with `usbipd attach --wsl --busid <id>`.
  The FEL device (`1f3a:efe8`) must be shared once from an admin shell (`usbipd bind --hardware-id 1f3a:efe8`);
  `fnirsi1013d-flash` attaches it to WSL by itself.
- **WSL**: the distro is `Ubuntu` (`wsl -d Ubuntu`), the repo is at `/mnt/d/src/fnirsi-1013d-ai`, and root is
  available without a password via `wsl -d Ubuntu -u root` for installing packages. `mcp/.venv` is the Windows venv;
  the WSL one is kept on the Linux side.
- **Reference checkouts** (read-only, outside this repo, never build inside them):
    - `D:\src\Fnirsi1013D` - Atlan4 fork (zips; extracted copies under `research/atlan4-extracted/`).
      `Verzia 1.xxx/FPGA for 1.xxx/` has the Verilog for AL3/EF2/EP4 and `FPGA commands.txt`.
    - `D:\src\FNIRSI_1013D_Firmware` - pecostm32 open firmware (clean base, V0.005).
    - `D:\src\FNIRSI-1013D-1014D-Hack` - pecostm32 RE: schematics, IC datasheets, `FPGA explained/` bus traces,
      emulator, `Test code/f1c100s_ch340` (USB serial on MUSB), flash backup/restore tools, Linux buildroot.
    - `D:\src\FNIRSI_1014D_Firmware` - Donwulff: Atlan 1.00o5 + 1014D port, PORT_AUDIT.md fixes, acqprobe tooling,
      headless Anlogic TD flow. Mine for fixes; not our base.
    - `D:\src\Anlogic_AL3-10_Analyzing` - pecostm32 bitstream RE; `FNIRSI-1013D_FPGA/Original_1013D` is a
      buildable TD re-implementation of the stock design with the correct pinout.
