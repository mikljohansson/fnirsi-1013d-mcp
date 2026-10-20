# Development

How the repository is laid out, how each part is built and tested, and how a change gets to the scope. This page is
for contributors; a user of the scope needs only the [install guide](guide/install.md).

Start your coding agent in the repository: [`AGENTS.md`](../AGENTS.md) and the per-directory `AGENTS.md` files
([`firmware/AGENTS.md`](../firmware/AGENTS.md), [`mcp/AGENTS.md`](../mcp/AGENTS.md),
[`fpga/al3/AGENTS.md`](../fpga/al3/AGENTS.md)) hold the context an agent (or a human) needs: layout, build commands,
the USB and FPGA facts learned on the bench, conventions. The root file has the rules; the per-directory files are the
handoffs between the agents that worked there, newest state first.

## Repository layout

```
AGENTS.md (project rules; Claude Code reads it when no CLAUDE.md exists), README.md
research/notes/        agent research digests (SYNTHESIS.md first)
research/tools/        analysis scripts (DPOS350P image parsing)
research/atlan4-extracted/, research/dpos350p/   extracted archives (gitignored, untrusted data)
firmware/scope/        scope application (base: Atlan4 v1.00o5, ported to a plain Makefile)
firmware/bootloader/   bootloader/updater work (later)
firmware/tools/        mksunxi, flashfilepacker, SD image + FEL load scripts, the SPI flash tool
fpga/al3/              Anlogic TD project for the AL3A10 (base: Atlan4 zaklad.v); dist/ holds the released
                       bitstreams with manifest.json and BITSTREAMS.md (checked by tools/bitstreams.py)
mcp/                   Python MCP server + serial client library + host-side analysis
tools/                 release SD package builder, bitstream manifest check, docs site hooks, screenshots
docs/                  the documentation site (mkdocs.yml at the root); plans/ holds the working papers
```

Details per part: [`firmware/README.md`](../firmware/README.md) (build, image format, SD layout, FEL, the screens,
calibration), [`mcp/README.md`](../mcp/README.md) (install, simulator, tools, bench CLIs, tests),
[`fpga/al3/README.md`](../fpga/al3/README.md) (toolchain, pin map, every bitstream change with its bench results)
and [`fpga/al3/TD_SETUP.md`](../fpga/al3/TD_SETUP.md) (the headless Anlogic TD flow).

## Building

### Firmware (C, bare-metal F1C100s)

The cross toolchain lives in WSL Ubuntu, or any Linux: `arm-none-eabi-gcc`, `make`, python3. From WSL with the repo
at `/mnt/d/src/fnirsi-1013d-ai`:

```
wsl -d Ubuntu -- bash -lc "cd /mnt/d/src/fnirsi-1013d-ai/firmware/scope && make -j"
```

| Target | What it gives |
|---|---|
| `make` | GCC 13 (apt `gcc-arm-none-eabi`): `build/fnirsi_1013d.bin`, an eGON.EXE image for SD sector 750 or FEL. Untested on hardware |
| `make gcc10` | Atlan4's GCC 10.3.1, the bench and release image: `build-gcc10-pc/fnirsi_1013d.bin`. `firmware/tools/fetch_gcc10.sh` downloads the compiler, checks each `.deb` against a pinned sha256 and unpacks it into `~/opt/gcc10` |
| `make gcc10-nommu` | the same code with the MMU off, version suffix `-nommu` |
| `make PC_DEBUG=1 BUILD=build-dbg` | bench build with the debug and force commands |
| `make -C host_tests test` | host gcc: frame layer, protocol vectors, session tests, UI and FFT tests |

Compiler flags are inherited from Atlan4 (`-mcpu=arm926ej-s -O3 -nostdlib`, custom `mem*.s`, tiny malloc, no libc).
Keep it that way; do not introduce libc or floating point in acquisition paths (the CPU has no FPU).

**Reproducible images.** The image depends only on the source, the toolchain and the commit: the identity is the
commit hash and the commit time read from git, never the wall clock or the checkout path, so the same commit gives
the same sha256 in any checkout and in CI (`firmware/README.md`, "Reproducible images"; the check is in
[`releasing.md`](releasing.md)).

### MCP server, host library and simulator (Python)

```
cd mcp
uv venv .venv
uv pip install --python .venv -e ".[dev]"
.venv/Scripts/python -m pytest -q          # Windows; .venv/bin/python on Linux
```

Plain `python -m venv` works too. In WSL keep the venv on the Linux side (for example `~/.venvs/fnirsi1013d-mcp`),
because `mcp/.venv` is the Windows one. Run ruff with the selection from `mcp/AGENTS.md` before pushing. Everything
runs against the built-in simulator (`fnirsi1013d-mcp --sim`); the scope's serial port is not needed for development.
Use `python -I` for anything that reads downloaded or untrusted files.

### FPGA (Verilog, Anlogic TD)

`fpga/al3/` holds the AL3A10 project and a headless flow, `build.sh`, that runs TD 4.6.8 SP1 under WSL and rebuilds
Atlan4's shipped 0x1532 byte for byte. TD and its licence come from anlogic.com after registering; the setup, the
stub libraries it needs and the build steps are in [`fpga/al3/TD_SETUP.md`](../fpga/al3/TD_SETUP.md). CI cannot run
TD, so the images are committed under `fpga/al3/dist/` and `tools/bitstreams.py check` verifies their size, header
and hashes against `manifest.json`. The testbenches are in `fpga/al3/tb/` (`tb/run.sh`).

### The documentation site

```
pip install -r tools/requirements-docs.txt
mkdocs build --strict        # what CI runs; mkdocs serve for a local preview
```

`docs/` is the single source. Links from a page to repository files outside the site are rewritten to GitHub by
`tools/mkdocs_hooks.py`; a link that resolves nowhere fails the strict build.

## The dev loop with hardware

Build with `make gcc10` in `firmware/scope`, then load the image into the scope's RAM without touching the card:

```
fnirsi1013d-flash                                   # default image build-gcc10-pc/fnirsi_1013d.bin
fnirsi1013d-flash --image firmware/scope/build/fnirsi_1013d.bin --port COM10
fnirsi1013d-flash --no-fel                          # the scope is already in FEL (boot menu)
```

The tool sends the protocol's ENTER_FEL command, hands the boot ROM device (`1f3a:efe8`) to WSL with `usbipd`, runs
`sunxi-fel -p write 0x7FFFFFE0 <image> exe 0x80000000` and waits for the serial port and `#VER` to come back. On a
Linux host it calls `sunxi-fel` directly. Without the ENTER_FEL capability, pick FEL in the boot menu and use
`--no-fel`. Only RAM is written; at the next power-on the scope boots from the card again.

When a build is good, `fnirsi1013d-flash --image <bin> --sha256 <hash> --persist` writes it to SD sector 750 through
the running firmware, after a verified backup of the sectors it replaces. The bootloader, calibration, settings and
boot-mode sectors are never touched by it. Exit codes, the retry rules and `fnirsi1013d-sd` are in
[`mcp/README.md`](../mcp/README.md), "Firmware dev loop".

The scope's serial port is exclusive: one program at a time. The MCP server frees it about 30 s after the last tool
call, so the flash tool and the server can share a bench.

## CI and releases

GitHub Actions run `.github/workflows/ci.yml` on every push and pull request to main: the GCC 10, GCC 13 and
PC_DEBUG builds, the host tests, ruff and pytest on Linux and Windows, the wheel smoke test, both protocol-vector
checkers, the bitstream manifest check and the strict docs build. Keep those green locally before pushing; every push
runs the full workflow. A `vX.Y.Z` tag runs `release.yml`. The procedure, the FEL hash check and the GPL reasoning
behind fetching Atlan4's binary-only bootloader instead of attaching it: [`releasing.md`](releasing.md).

## Contributing

Issues and pull requests are welcome, from people and from agents. Add host tests for protocol changes and update
[`protocol.md`](protocol.md), add user-visible changes to [`CHANGELOG.md`](../CHANGELOG.md) under Unreleased, update
the relevant `AGENTS.md`, and never flash a device without a verified backup. Small, reviewable commits, one topic
each; push once per effort, not after every commit.

Reports from other units are the most useful contribution: the FPGA variant and the version word (Diagnostics,
Status), the firmware version (`#VER` or Diagnostics), what you did and what you saw, whether the calibration wizard
passes (a GET_CAL dump or the Diagnostics report saved to the card). The entries in the
[bench log](hardware-status.md) are a good model.
