# Pre-tag checklist, v0.3.0

The coordinator ticks this before `git tag -a v0.3.0`. Procedure and commands: `docs/releasing.md` section 2;
background and the findings F1-F12: `docs/plans/release-readiness.md`; gate: `docs/plans/roadmap.md` section 5.
Record each tick with the date and the commit or log line that proves it. Untick a box if its input changes
afterwards (a new firmware commit invalidates every box from "Versions" down).

## Content

- [ ] Every epic of `docs/plans/roadmap.md` sections 1-3 merged and benched, or removed from the list by the owner.
- [ ] Owner verdicts in `docs/hardware-status.md` for the built-but-unbenched items (calibration, phosphor, generator rows,
      USB long idle, UI steps), or the item named under "Known issues" in the release notes.

## FPGA bitstreams

- [x] 0x153A pinned by fpga-gen9 (2026-10-20; was 0x1538 by fpga-gen7): built from a committed `fpga/al3/src` (`SRC_REV=<commit> ./build.sh`), testbench run, hashes
      in `fpga/al3/README.md`.
- [x] 0x153A flashed on the owner's unit and benched 2026-10-20 (`#VER fpga=0x153a`, wizard trains 0x08E3, band sweep clean; was 0x1538): the full-rate encode check clean, the calibration
      wizard passes, the release candidate firmware runs on it (`docs/hardware-status.md` line).
- [x] `fpga/al3/dist/al3-0x153A.bin` added (2026-10-20 recommended); its `manifest.json` entry (size, sha256, padded sha256, full source commit,
      `min_firmware`, `status: recommended`, bench record); the 0x1538 rows of `BITSTREAMS.md` filled.
- [ ] Status of the older images decided (0x1533-0x1536 `supported` / `unbenched`, 0x1537 `withdrawn`), and the
      "Use the recommended image" paragraph of `BITSTREAMS.md` still true.
- [ ] `python3 -I tools/bitstreams.py check --release` exits 0.
- [ ] Owner decision recorded: bitstreams are committed under `fpga/al3/dist/` and attached to releases
      (`docs/releasing.md` section 5).

## Versions and notes

- [ ] `firmware/scope/pc_cmds.h`: `#define PC_FW_VERSION "0.3.0" ...` (the one quoted literal).
- [ ] `mcp/pyproject.toml`: `version = "0.3.0"`.
- [ ] `CHANGELOG.md`: `## [Unreleased]` first and empty, `## [0.3.0] - YYYY-MM-DD` below it and non-empty; the obsolete
      "Plan for the first tag" block and "Nothing has been tagged yet" removed; the 0.2.46-0.2.50 sub-sections in order.
- [ ] `docs/release-notes/0.3.0.md`: every `{{placeholder}}` filled (screenshots, tested-on list, known issues), each fact
      checked against `docs/hardware-status.md` once more, user-facing wording only.
- [ ] `python .github/scripts/release_meta.py --tag v0.3.0 --notes notes.md` exits 0; `notes.md` read as a visitor would.

## Build and CI

- [ ] `release: v0.3.0` commit on main, CI green: firmware (GCC 10, GCC 13, PC_DEBUG, host tests), mcp on Linux and
      Windows, **wheel** on Linux and Windows, protocol vectors, CHANGELOG structure, bitstream check.
- [ ] Local `make gcc10` sha256 of that commit equals the sha256 the CI firmware job prints (reproducibility).
- [ ] `python tools/make_release_sd_package.py build --scope <image> --parts parts` run against the real Atlan4 download
      the day before the tag (F7).

## SD package and bench

- [ ] SD parts built from the CI image of the release commit with the tools zip, written to a spare card prepared as
      `docs/guide/install.md` says (FAT32 partition at sector 16384, 8 MiB) with `make_sd_image.py write`; the same
      command on a card partitioned at 2048 refuses and names the start it needs; the scope boots the prepared card: boot menu, PECO preselected, `#VER fw=0.3.0`, the Diagnostics
      identity line shows the release commit.
- [ ] Release candidate persisted to the owner's card (partition at 8192; fw 0.2.101+ writes up to the partition, the
      image ends at about 2261); 30-minute soak with the MCP server polling (`fnirsi1013d-soak`)
      passes; the long-idle check passes (roadmap section 5 item 4).
- [ ] First-run screen and calibration wizard on the release candidate with the recommended bitstream.

## Dry run

- [ ] `v0.3.0-rc1` tag pushed on the release commit; `release.yml` green (check, ci, publish).
- [ ] The rc release page checked: notes, the assets (`fw.bin`, `sdtools.zip`, wheel, sdist, the bitstreams and their
      `-flash1m` images, `BITSTREAMS.md`, `SHA256SUMS`), marked pre-release; `sha256sum -c SHA256SUMS` on a download.
- [ ] The rc release and tag deleted.

## Presentation

- [ ] Screenshots taken with `scope_screenshot` and committed under `docs/img/`: `scope.png` (scope view with cards and
      the MCP chip), `spectrum.png`, `diagnostics.png` (Status page), `bode.png` (plot of `fnirsi1013d-bode` output); the
      README screenshot block un-commented; the same images in the release notes.
- [ ] README links checked (every relative link resolves on GitHub).
- [ ] Credits and AI attribution wording confirmed by the owner (README "Credits", F12).
- [ ] GitHub repository description, topics, Discussions on if the announcement uses them, issue templates (F11).
- [ ] Vendored `research/tools/vendor/capstone/lib/capstone.dll` licence notice checked (F11).
- [ ] `docs/plans/announcement.md` final: `{{VERSION}}`, `{{RELEASE_URL}}` (the tag URL, not `/latest`: v0 releases are
      pre-releases), screenshots, clip, `{{BITSTREAM_NOTE}}` (the bitstreams are attached to the release, with
      `BITSTREAMS.md`), `{{TESTED_ON}}`.

## Tag and after

- [ ] `git tag -a v0.3.0 -m "fnirsi-1013d-mcp 0.3.0"`, `git push origin v0.3.0`, `release.yml` green.
- [ ] FEL check of the attached image (`docs/releasing.md` section 4): read-back identical, `#VER fw=0.3.0`, build hash
      equals `git rev-parse --short=8 v0.3.0`; noted in `docs/hardware-status.md`.
- [ ] `docs/releasing.md` section 7: the two open boxes ticked.
- [ ] Announcement posted once (EEVblog thread, the repository, the subreddits in the draft).
