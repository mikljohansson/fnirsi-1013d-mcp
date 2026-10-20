# Release readiness for v0.3.0

Read-through of `docs/releasing.md`, `tools/make_release_sd_package.py`, `.github/workflows/release.yml`,
`.github/workflows/ci.yml`, `.github/actions/firmware-build/action.yml`, `.github/scripts/release_meta.py` and the
README "Getting started", against main 9cc599c (firmware 0.2.51, protocol 1.15). Written 2026-10-19. Nothing here has been
applied; it is a list for the coordinator. Findings are numbered F1..; the tick list in section 4 refers to them.

Update 2026-10-19 (release prep): F1 fixed in `docs/releasing.md`; F2 and F5 resolved by separate user-facing release notes
(`docs/release-notes/X.Y.Z.md`, read by `release_meta.py`); F3 implemented (bitstreams committed under `fpga/al3/dist/`,
attached by `release.yml`, `BITSTREAMS.md`); F6 has a dry-run path (`vX.Y.Z-rcN` tags); the README follows section 5. The tick
list the coordinator uses is now `docs/plans/release-checklist.md`.

Verdict: the workflow chain is coherent and the checker works (run on this tree: `release_meta.py --check-changelog` passes,
`--tag v0.3.0` fails with exactly the three expected errors: firmware version 0.2.51, mcp version 0.1.0, no `[0.3.0]` section).
What is not ready is mostly what an outside user meets: no way to get the FPGA bitstreams, a README that still describes the
project as it was on 2026-10-18, and an MCP package that only works from a checkout.

## 1. Every manual step, in order

From `docs/releasing.md` section 2 and the workflow files. "Owner" means it needs the scope or the owner's decision.

| # | Step | Who | Notes |
|---|---|---|---|
| 1 | Version bump: `PC_FW_VERSION` in `firmware/scope/pc_cmds.h` and `version` in `mcp/pyproject.toml`, both `0.3.0` | coordinator | F1: the doc describes two lines, there is one |
| 2 | `CHANGELOG.md`: new `## [0.3.0] - YYYY-MM-DD`, leave an empty `## [Unreleased]` | coordinator | F2: write it, do not move the dev diary |
| 3 | Local checks: `make gcc10`, `make`, `make -C host_tests test` (WSL), `pytest -q` in `mcp`, `python .github/scripts/release_meta.py --tag v0.3.0 --notes notes.md` | coordinator | exit 0 means the tag is accepted |
| 4 | Local dry run of the SD tools against the real download: `python tools/make_release_sd_package.py build --scope <image> --parts parts` | coordinator | F7: needs network, tests the pin |
| 5 | Commit `release: v0.3.0` on main, push, wait for CI | coordinator | the tag run repeats the whole CI |
| 6 | `git tag -a v0.3.0 -m "fnirsi-1013d-mcp 0.3.0"`, `git push origin v0.3.0` | coordinator | one-way: a published release is never re-tagged |
| 7 | Watch `release.yml`: check, ci, publish | coordinator | F6: first run of this workflow ever |
| 8 | Download the assets, `sha256sum -c SHA256SUMS`, FEL-load the image, compare read-back, check `#VER` and the build hash (`docs/releasing.md` section 4) | owner | bench step |
| 9 | Note the result in `docs/hardware-status.md` | coordinator | release, image sha256, FEL read-back identical, `#VER` |
| 10 | Tick the two open boxes in `docs/releasing.md` section 7 (artifact download, `contents: write`) | coordinator | |
| 11 | Update README "Status", post the announcement (`docs/plans/announcement.md`), enable Discussions if the draft uses them | owner | F11, F12 |

## 2. Mismatches and risks found

Severity: **blocker** (the release would be wrong or unusable), **fix** (should change before the tag), **note**.

**F1 (fix) releasing.md step 1 is stale.** It says to change "both `PC_FW_VERSION` lines, `"X.Y.Z"` and `"X.Y.Z-m1"`". Since fw 0.2.48
there is one line, `#define PC_FW_VERSION "0.2.51" PC_FW_VERSION_MAP PC_FW_VERSION_MMU` (`pc_cmds.h:32`), with the `-m1` and `-nommu` suffixes
as macros. `release_meta.py` reads it correctly (its regex takes the first quoted literal), so the workflow is fine; only the doc is wrong.
Also `docs/releasing.md` line 65 points to "`docs/plans/roadmap.md` section 1"; the release gate is now section 5 of the rewritten roadmap.

**F2 (blocker for the notes) CHANGELOG release notes would be the whole dev diary.** The release notes are the body of the `[0.3.0]`
section. Moving "the entries from Unreleased" (releasing.md step 2) puts about 45 KB, 59 firmware sub-sections from 0.1.0 to 0.2.51, in the
GitHub release. Also in the way: the Unreleased intro says "Nothing has been tagged yet", the block "Plan for the first tag (v0.3.0)" is obsolete, and
the 0.2.x sub-sections are out of order around 0.2.46-0.2.50 (0.2.50, 0.2.47, 0.2.48, 0.2.49, 0.2.46). Proposal: the `[0.3.0]` section is a
curated page (what it is, highlights, install in five lines, caveats, links), and the per-build history goes under a `### Development builds 0.1.0-0.2.51`
heading further down in the same section or in `docs/changelog-dev-builds.md` (keep the commit citations). The structure check in
`release_meta.py` only needs `## [Unreleased]` first and `## [X.Y.Z] - date` after it. Firmware agents are editing the file in parallel; do this last.

**F3 (blocker) FPGA bitstreams are not obtainable.** `fpga/al3/bitstreams/` holds only a README; `*.bin` is gitignored; the release attaches the scope image, the SD
tools zip, the wheel/sdist and `SHA256SUMS` and nothing else. Atlan4's 0x1532 is available from his repository, but the EXT trigger (0x1533), the
decimation filters (0x1534), the FIR (0x1535) and segmented capture (0x1536) exist only on the owner's disk and as TD sources that need a vendor tool and a
licence. A reader of the announcement cannot use half the headline features. Decision for the owner: attach the pinned padded 1 MiB images (and
their sha256) to the release. The source is GPL in the repository and the build is documented as deterministic (`fpga/al3/README.md`, byte-identical
rebuild of 0x1532), so the GPL reasoning in `docs/releasing.md` section 5 does not apply to them as it does to Atlan's bootloader. If yes, add them to
`release.yml` (the artifact would have to come from a committed file or a CI build; TD is not available in CI, so commit the `.bin` files under
`fpga/al3/bitstreams/` with a git-tracked exception or attach them by hand with `gh release upload`) and extend the `SHA256SUMS` step. Also add
a "which feature needs which bitstream" table to the README.

**F4 (fix) the MCP package works only from a checkout.** The wheel is attached and the draft announcement says "pip install the wheel", but:
`server.py:63` (`_default_mcp_dir`, `parents[2]`) puts the default `data/` and `logs/audit.jsonl` next to the installed package, i.e. inside
`site-packages` or the venv's `Lib`; `flash.py:51`, `bench.py:30`, `sdcli.py:29` use `parents[3]` as the "repo" for the default image path and the
`backups/` directory; `vectors.py:31` expects `docs/protocol-vectors.json`. The `.mcp.json` in the repository hard-codes
`mcp/.venv/Scripts/fnirsi1013d-mcp.exe` and `COM10`. Fix before the tag (small): default data and audit directories under a per-user directory
(`platformdirs`-style, or the current directory) when the package is not inside a checkout, `--backup-dir` required or defaulting to `./backups` for
the flash and sd tools, and a documented `claude mcp add` line for the wheel. Otherwise the README must say "install from a checkout" and the announcement must not
suggest the wheel. Test the wheel in a clean venv: `python -m build mcp`, install the wheel, run `fnirsi1013d-mcp --sim`, call one tool, confirm where files land.

**F5 (note) release notes carry no install block.** `release.yml` appends a file list and the hashes to the CHANGELOG text and then links to
`docs/releasing.md`. A first-time visitor on the release page needs the five install lines. Put them at the top of the `[0.3.0]` section (F2).

**F6 (note) the release job has never run.** `docs/releasing.md` section 7 leaves two boxes open: that `download-artifact` finds the `firmware-image` artifact
uploaded inside the reusable `ci.yml`, and that `gh release create` works with the job-level `contents: write`. `release_meta.py` accepts only
`vMAJOR.MINOR.PATCH`, so there are no throwaway `-rc` tags. Dry run on a scratch fork: push main, set versions and a `[0.2.99]` section on a temp
branch, tag `v0.2.99`, watch `release.yml`, delete the release and the tag. Do this before the real tag; a failed run on the real tag costs a patch version
(releasing.md section 2, "Do not move a tag").

**F7 (note) the SD tools depend on a download of a pinned Atlan4 commit.** `make_release_sd_package.py` fetches
`raw.githubusercontent.com/Atlan4/Fnirsi1013D/<commit>/Verzia 1.xxx/DSO 1.xxx/backup dso/v1.00o5.zip`, checks the two inner files by sha256 and refuses
anything else. The release job runs this on every release, so a vanished or renamed file fails the release job after the tag. Run step 4 of section 1 the
day before. Keep a copy of the zip (the owner has one, `research/atlan4-extracted/`); `--local` accepts it. The script and the zip's member paths are
otherwise consistent with what the workflow does (`--scope`, `--parts`, `-I`).

**F8 (note) all v0 tags are pre-releases**, so the repository's "Latest release" slot on GitHub stays empty and `/releases/latest` returns 404. That is
intended (CHANGELOG and releasing.md say so), but the announcement link must be the tag URL, not `/latest`.

**F9 (note) the tools zip has no documentation.** `fnirsi_1013d-sdtools-X.Y.Z.zip` holds three scripts. Its instructions live only in
`docs/releasing.md` section 3 and the script's `--help`. Add a short `README.txt` to the zip (the four commands and the "do not `dd` the package" warning)
or link it from the release notes (F5).

**F10 (fix) stale facts that an outside reader will hit** (README is covered in section 3):

- `firmware/README.md` section 3, end: "Tested here only on image files ... No physical card has been written." False since Tier 1 (`docs/hardware-status.md`
  checklist, 2026-10-18). Also "`\\.\PhysicalDriveN` on Windows (untested)": check what was used and update.
- `docs/hardware-prep.md` header: "Status: DRAFT for owner review ... Nothing in this guide has been executed on the owner's unit yet." The guide was executed.
- `CLAUDE.md` is cited as the rules file in `docs/hardware-prep.md`, `docs/hardware-status.md`, `firmware/README.md`, `firmware/tools/README-spiflash.md`, `fpga/al3/*`, and
  `mcp/src/fnirsi1013d/flash.py`; the file is `AGENTS.md` and `CLAUDE.md` does not exist in the tree. Either add a one-line `CLAUDE.md` that points to `AGENTS.md`
  (Claude Code reads `CLAUDE.md`, and the skill and `.mcp.json` assume the repo is opened in Claude Code) or replace the references.
- `firmware/scope/pc_cmds.c` comments at 1777 and 1834 say "v4 tracking from 1.14 / row 53"; it is protocol 1.15, row 54 (`docs/protocol.md` is corrected on
  this branch). A comment-only change for the next firmware agent.
- The task queue still shows the dead-code purge unticked although fw-gen7 merged it (a86cd6c, 0.2.47), and the "morning bench" item (5) lists flashing 0x1535, which
  was done 2026-10-19 10:33 (`docs/hardware-status.md`).

**F11 (fix) repository presentation.** Not in any workflow: a GitHub description and topics, Discussions enabled (the draft announcement uses them), an issue template for
"bench report" and "bug" (firmware version, FPGA version, variant, steps), and a screenshot or two in the README (section 3). `LICENSE` exists. The repository
contains a vendored 7 MB `capstone.dll` (releasing.md section 6 already asks to check its licence notice); confirm that before a lot of people clone it.

**F12 (note) credits line.** README "Credits" names the model as "Claude (Fable 5.1, via Claude Code agents)". Confirm the exact wording the owner wants for AI
attribution before the announcement links to it.

## 3. README "Getting started" through an outsider's eyes

Walked through as someone who owns a 1013D and found the announcement. Each item is where I would stop.

1. **Where do I get the thing?** Step 1 sends me to `docs/hardware-prep.md`; step 2 to `firmware/README.md` section 3 and `firmware/tools/make_sd_image.py`, i.e. the
   checkout. The paragraph "Releases, once tagged, attach ..." is the only mention of the release assets and it is in the future tense. There is no sequence "download these
   three files, run these two commands" in the README. After the tag it should read as the install path (the commands of `docs/releasing.md` section 3).
2. **Which features need the FPGA step?** Step 1 says "optionally flash Atlan4's AL3 bitstream for 16k samples". The README bullets advertise features (decimation filters, FIR,
   external trigger) that need our bitstreams 0x1533-0x1535, which a reader cannot get (F3). Without a table "feature -> minimum bitstream -> where to get it" they will assume the
   firmware alone gives them everything.
3. **The programmer.** "A 3.3 V SPI programmer on the config-flash header (an FT2232D board works via `firmware/tools/spiflash_d2xx.py`)" needs soldering a 2x3 header and a
   pinout that `docs/hardware-prep.md` calls "reconstructed", with the header orientation found by continuity. A reader needs the risk stated up front, next to the programmer
   requirement: wrong pin one can damage the board; back up the 25Q80 twice.
4. **Hardware identification.** "This supports AL3A10" is stated, but how to tell on the scope itself (open the case, read the FPGA marking, or look at the FPGA version on the
   diagnostics screen after the open firmware boots) is a link away. Say in the README that an EF2L45 or EP4CE6 board is untested and what to report.
5. **SD card.** Writing "to a spare micro SD card" with `make_sd_image.py` is correct, but the `dd` trap (zeroes sectors 708-710) is only in `firmware/README.md` and `releasing.md`. One
   warning sentence belongs in the README step. The FAT32 partition must start at sector 2048 or later, GPT cards are refused: also not in the README. (16384, 8 MiB, since fw 0.2.101.)
6. **First boot.** No description of what they should see (the boot menu with PECO / FEL / FNIRSI / Update buttons, boot mode 4), how to get back to stock, or that the base
   calibration is the first thing to do ("Calibrate" is section 4, after "Use it").
7. **MCP install.** `python -m venv .venv && .venv/Scripts/activate` fails in PowerShell (it is `.venv\Scripts\Activate.ps1`) and in cmd it is `.venv\Scripts\activate`; the
   forward-slash form works in Git Bash only. `--port COM10` is the owner's port, `auto` finds the scope. `pip install -e .` needs the checkout (F4). "No driver needed on Windows, Linux
   or macOS": only Windows and Linux (WSL) have been run against the hardware; macOS is a claim from the USB class, not a test. Say so or drop macOS.
8. **The repository's `.mcp.json`** names a Windows venv path and COM10. On Linux Claude Code will show the `fnirsi1013d` server as failed. The simulator entry works only on Windows for
   the same reason. Document the edit, or ship `.mcp.json.example` and keep the owner's file out of the tree.
9. **Firmware updates and the dev loop** (`fnirsi1013d-flash`) need WSL, usbipd and sunxi-fel on Windows (`mcp/README.md`, `firmware/README.md` section 4). The README's one-line
   mention suggests it is a plain command. It is the developer path, not the install path; move it below "Development".
10. **Dead or stale statements.** "Status" says firmware 0.2.48, protocol 1.13, nothing tagged, unit boots 0.2.25, "0.2.48 MMU image had not been run" (it was, 2026-10-19). The feature
    list says "MCP server with 20+ tools" (27 in `server.py`), "protocol 1.13", "owner's unit runs 0x1534" (it runs 0x1535 since 2026-10-19 10:33). "Verified lineage ... rebuilding his
    released binary byte for byte" sits next to "the byte-identical check ... gone". "Roadmap and community wishes" lists Bode as future and deeper memory first; the new roadmap is
    `docs/plans/roadmap.md`. The calibration text still describes the pre-wizard flow (two steps, both probes, box), the wizard replaced it in 0.2.50.
11. **Safety framing.** The README has no sentence about the risk of flashing, the ownership of the damage, or the generator pin driving real hardware (tier 3). The MCP safety tiers are a
    selling point: say it in the intro.
12. **No pictures.** A scope project without one screenshot. `scope_screenshot` exists.

## 4. Checklist to tick before tagging v0.3.0

Order is roughly the order of work. IDs refer to section 2.

Content freeze and docs

- [ ] All epics of `docs/plans/roadmap.md` sections 1-3 merged, or removed from the list by the owner.
- [ ] Bench lists for the built items ticked in `docs/hardware-status.md` (calibration 6.3-6.5, phosphor, generator rows, USB 2-hour idle, UI steps).
- [ ] FPGA 0x1536 flashed and benched (check 5), or dropped from the release scope and from the announcement text.
- [ ] Decision on bitstream distribution made and implemented (F3), `fpga/al3/bitstreams/README.md` updated.
- [ ] README rewritten (section 5 proposal), links checked, "Status" removed or current, screenshots in (`docs/img/` or the release assets) (F10, section 3).
- [ ] `docs/releasing.md` step 1 and the roadmap reference corrected (F1); the two section 7 boxes ticked after the real run.
- [ ] Stale statements fixed (F10): `firmware/README.md` "no physical card", `docs/hardware-prep.md` status line, `CLAUDE.md` references, `pc_cmds.c` comments, task queue.
- [ ] MCP package works installed from the wheel in a clean venv (F4), or the docs say "from a checkout" and the announcement does not mention the wheel.
- [ ] `.mcp.json` / example and the Claude Code and Codex setup text work on Linux (F4, section 3 item 8).
- [ ] Release-notes text written (F2, F5), including the install lines and the caveats (AL3A10 only, programmer needed, stock firmware stays).

Repository and CI

- [ ] CI green on the release candidate commit on main (firmware GCC 10/13/PC_DEBUG, host tests, mcp Linux and Windows, vectors, CHANGELOG structure).
- [ ] Vendored `capstone.dll` licence notice checked (F11).
- [ ] GitHub repository description, topics, Discussions on, issue templates, social preview (F11).
- [ ] Dry run of `release.yml` on a scratch fork with a throwaway tag (F6).
- [ ] `python tools/make_release_sd_package.py build ...` run against the real download the day before (F7).

Version and tag

- [ ] `PC_FW_VERSION` and `mcp/pyproject.toml` both `0.3.0`.
- [ ] `CHANGELOG.md`: `## [0.3.0] - YYYY-MM-DD` present and non-empty, `## [Unreleased]` first and empty; `release_meta.py --tag v0.3.0` exits 0.
- [ ] Local build `make gcc10` sha256 equals the sha256 CI prints for the same commit (reproducibility, releasing.md section 1).
- [ ] A release candidate of the exact commit is persisted to the SD card; 30-minute soak with the MCP server polling passes; the long-idle test passes (roadmap section 5).
- [ ] `git tag -a v0.3.0`, push the tag, `release.yml` finishes green (check, ci, publish).

After the tag

- [ ] Release has the five assets (`.bin`, `sdtools.zip`, wheel, sdist, `SHA256SUMS`) plus the bitstreams if F3 says yes; release page marked pre-release (F8).
- [ ] Download and `sha256sum -c SHA256SUMS`; FEL read-back of the attached image identical; `#VER` says `fw=0.3.0`; build hash equals `git rev-parse --short=8 v0.3.0`.
- [ ] The tools zip run on a clean machine builds an SD package and parts (the CI test did, repeat once by hand); write the parts to a spare card; the scope boots it.
- [ ] Result noted in `docs/hardware-status.md` and `docs/releasing.md` section 7.
- [ ] Announcement posted once (`docs/plans/announcement.md` placeholders filled: version, release URL, screenshots, clip, bitstream note, tested-on list).

## 5. Proposed README structure for first-time visitors (not applied)

The current README is a project diary: 200 lines, most of it feature bullets with internal history. Proposal: the first screen answers "what is it and is it for me", the
second shows it, install is a numbered path, and everything about how the project was built moves to `docs/` and `AGENTS.md`. Target length about 150 lines.

1. **Title, badges, one-line tagline.** CI badge, release badge, licence. "Open firmware, FPGA changes and an MCP server for the FNIRSI 1013D tablet oscilloscope."
2. **What it is, in two sentences.** "Replaces the scope application of the FNIRSI 1013D (Anlogic AL3A10 variant) with an open firmware that runs from the SD card, and adds a USB protocol and an
   MCP server so that an AI agent can configure, capture and analyse while you use the touch screen. The stock firmware is never touched." One sentence of warning: flashing the FPGA needs an SPI programmer; back
   up first.
3. **Screenshot block.** Three or four images in a row: the scope view with the MCP chip, the spectrum view, the phosphor view, an agent session transcript (or a short GIF). Alt text with the facts.
4. **Features** (about ten bullets, one line each, a number or a link with each): spectrum analyser (4096-point FFT in 2.7 ms), FPGA decimation filters (alias 17 -> 76 dBV, with source), per-core calibration,
   phosphor, EXT trigger, generator and Bode, collaborative mode, MCP with 27 tools and safety tiers, hands-free dev loop, reproducible builds. Link to `firmware/README.md` for depth. Table "feature -> needs
   firmware only / needs bitstream 0x153x".
5. **Hardware and caveats.** Supported: AL3A10 (verified). Untested: EF2L45, EP4CE6. Real limits (200 MS/s, 8 bit, 30 MHz, no DAC). What stays stock. Risks.
6. **Install** (numbered, release-based): (1) back up; (2) download the release assets and check the hashes; (3) build the SD parts with the tools zip and write them to a spare card, with the `dd` warning; (4) optional FPGA
   bitstream with the programmer, link to `docs/hardware-prep.md`; (5) first boot and the calibration wizard; (6) get back to stock. Each step one or two lines and a link.
7. **MCP quick start.** Install (`pip install` the wheel, or from a checkout), `fnirsi1013d-mcp --sim` to try without hardware, `--port auto`, then the Claude Code and Codex snippets, and three example prompts.
   Safety tiers in two lines, the audit log, the skill `.claude/skills/fnirsi-scope`.
8. **Documentation map.** A small table: protocol (`docs/protocol.md`), tools (`docs/mcp-tools.md`), measurement guide (`docs/agent-measurement-guide.md`), hardware prep, bench log, firmware README, FPGA README,
   roadmap.
9. **Status and roadmap.** Three lines: version, what was benched on what hardware, link to `docs/plans/roadmap.md` and the CHANGELOG. Replaces the current "Status" and "Roadmap and community wishes".
10. **Development.** The existing "Development" text, shortened: build in WSL/Linux, host tests, the FEL dev loop (this is where `fnirsi1013d-flash` belongs), `AGENTS.md`.
11. **Contributing and bench reports.** What to send (version, variant, steps, GET_CAL dump), link to the issue templates.
12. **Credits.** As today: pecostm32, Atlan4, Donwulff, the EEVblog thread, and the Claude/owner attribution line.
13. **Licence.** GPL-3.0, one line on why (inherited).

Items to drop from the README: "Removed options", "Link robustness fixes", "Verified lineage", the byte-identical history, the MMU A/B build note, the long calibration paragraph (replace with a link to the wizard
section). All stay in `CHANGELOG.md` and `firmware/README.md`.
