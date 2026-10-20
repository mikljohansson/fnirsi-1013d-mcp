# Pre-tag checklist, v0.3.0

The bench session ticks this before `git tag -a v0.3.0`. Procedure and commands: `docs/releasing.md` section 2;
background and the findings F1-F12: `docs/plans/release-readiness.md`; gate: `docs/plans/roadmap.md` section 5.
Record each tick with the date and the commit or log line that proves it. Untick a box if its input changes
afterwards (a new firmware commit invalidates every box from "Versions" down).

Paperwork pass by rel-gen2 on 2026-10-20 (branch `rel-gen2` from 876700a): the boxes below that it ticked name that
branch's commit; the maintainer's items and the hardware items are left open.

## Content

- [ ] Every epic of `docs/plans/roadmap.md` sections 1-3 merged and benched, or removed from the list by the maintainer.
      (The roadmap's state line still reads fw 0.2.80 / 0x153A; it needs reconciling first.)
- [ ] Maintainer verdicts in `docs/hardware-status.md` for the built-but-unbenched items (calibration, phosphor, generator rows,
      USB long idle, UI steps), or the item named under "Known issues" in the release notes.

## FPGA bitstreams

- [x] 0x153B pinned by fpga-gen11 / fpga-gen12 (2026-10-20; 0x153A by fpga-gen9 before it): built from a committed
      `fpga/al3/src` (source c173ed5), testbench 13 steps ALL PASS on seeds 12345 and 4242, hashes in
      `fpga/al3/dist/BITSTREAMS.md` and `manifest.json`.
- [x] 0x153B flashed on the maintainer's unit 2026-10-20 19:30-19:32 (verified 1391978c..) and benched 20:40-20:49 on fw 0.2.102
      (`docs/hardware-status.md` "0x153B benched good" and "the remaining 0x153B checks": 0 of 100 untriggered captures
      spliced, ten sweeps without retakes, checks 3 and 6 contiguous); the wizard passed on it at 19:52-19:58. 0x153A was
      flashed and benched 10:03-10:49 (wizard trains 0x08E3, band sweep clean) and stays supported.
- [x] `fpga/al3/dist/al3-0x153B.bin` added, `manifest.json` entry with `status: recommended`, `min_firmware` 0.2.97 and the
      bench record (recommended since 2026-10-20 20:42); 0x153A `supported`.
- [ ] Status of the older images decided, and the "Use the recommended image" paragraph of `BITSTREAMS.md` still true.
      The manifest has 0x1533-0x1535 `supported`, 0x1536 `unbenched`, 0x1537-0x1539 `withdrawn`, 0x153A `supported`, and
      the paragraph is true (0x153B is the newest). Left open: the 0x1538 row of `BITSTREAMS.md` still reads "unbenched
      (flash session pending)" and the 0x1538 / 0x1539 Bench cells "not yet", against the manifest's `withdrawn` and its
      flash records of 2026-10-19 (bench session: fix the two rows).
- [x] `python3 -I tools/bitstreams.py check --release` exits 0 (2026-10-20 on 876700a, rel-gen2: "9 bitstreams
      consistent"). Run again at the release commit.
- [ ] Maintainer decision recorded: bitstreams are committed under `fpga/al3/dist/` and attached to releases
      (`docs/releasing.md` section 5).

## Versions and notes

- [ ] `firmware/scope/pc_cmds.h`: `#define PC_FW_VERSION "0.3.0" ...` (the one quoted literal).
- [ ] `mcp/pyproject.toml`: `version = "0.3.0"`.
- [ ] `CHANGELOG.md`: `## [Unreleased]` first and empty, `## [0.3.0] - YYYY-MM-DD` below it and non-empty; the obsolete
      "Plan for the first tag" block and "Nothing has been tagged yet" removed; the 0.2.46-0.2.50 sub-sections in order.
      The section text and the steps are ready in `docs/release-notes/changelog-0.3.0-draft.md` (rel-gen2, 517f252);
      delete that file in the release commit.
- [x] `docs/release-notes/0.3.0.md`: every `{{placeholder}}` filled (screenshots, tested-on list, known issues), each fact
      checked against `docs/hardware-status.md` once more, user-facing wording only (2026-10-20, rel-gen2, c0ea0c1;
      `mkdocs build --strict` passes). Re-check the tested-on line after the release-candidate bench.
- [ ] `python .github/scripts/release_meta.py --tag v0.3.0 --notes notes.md` exits 0; `notes.md` read as a visitor would.
      Dry-run 2026-10-20 (rel-gen2) on a scratch tree with both versions at 0.3.0 and the draft CHANGELOG section applied:
      `--tag v0.3.0`, `--tag v0.3.0-rc1` and `--check-changelog` exit 0. Run it on the release commit itself.

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
- [x] A persisted release candidate boots from the maintainer's card (partition at 8192; fw 0.2.101+ writes up to the partition,
      the image ends at about 2261): fw 0.2.101 persisted 2026-10-20 19:22 and cold-booted 19:24, the first image over 776
      sectors through Atlan4's v0.8 bootloader; fw 0.2.102 persisted 19:27 and cold-booted 19:29 with every Diagnostics tile
      OK (`docs/hardware-status.md` 19:22, 19:24, 19:29). Repeat for the 0.3.0 image.
- [x] 30-minute soak with the host polling passes: 2026-10-20 21:26-21:56 on fw 0.2.102 / 0x153B, 899 status polls, 15 health
      reads, 15 captures, 0 errors (bench log). To repeat on the 0.3.0 image if the release commit changes firmware code.
- [ ] The long-idle check passes (roadmap section 5 item 4).
- [ ] First-run screen and calibration wizard on the release candidate with the recommended bitstream (the wizard passed
      on fw 0.2.102 with 0x153B on 2026-10-20 19:52-19:58; the first-run screen and the 0.3.0 image remain).

## Dry run

- [ ] `v0.3.0-rc1` tag pushed on the release commit; `release.yml` green (check, ci, publish).
- [ ] The rc release page checked: notes, the assets (`fw.bin`, `sdtools.zip`, wheel, sdist, the bitstreams and their
      `-flash1m` images, `BITSTREAMS.md`, `SHA256SUMS`), marked pre-release; `sha256sum -c SHA256SUMS` on a download. The
      notes' screenshots are raw URLs pinned to commit 876700a, so they show on the rc page too.
- [ ] The rc release and tag deleted.

## Presentation

- [x] Screenshots taken with `scope_screenshot` (through `tools/screenshots.py`, which writes `docs/screenshots/`, not
      `docs/img/`) and committed: `scope.png` (scope view with cards and the MCP chip, fw 0.2.96, 5d37c4f),
      `spectrum.png` (eb645ec), `diagnostics.png` (Status page all OK on fw 0.2.102 from the card, 49c295f, 19:29), and in place of a
      host `bode.png` the on-device `frequency-response-sweep.png` (edd6f85). The same images in the release notes
      (rel-gen2, c0ea0c1).
- [ ] README screenshot block un-commented (it still points at `docs/img/`; moved to the README rewrite, maintainer
      2026-10-20: use `docs/screenshots/*.png`).
- [ ] README links checked (every relative link resolves on GitHub). Checked 2026-10-20 on 876700a: 39 relative links,
      all resolve to tracked files except the four `docs/img/*.png` inside the commented-out screenshot block; re-check
      after the README rewrite.
- [ ] Credits and AI attribution wording confirmed by the maintainer (README "Credits", F12).
- [ ] GitHub repository description, topics, Discussions on if the announcement uses them, issue templates (F11).
- [ ] Vendored `research/tools/vendor/capstone/lib/capstone.dll` licence notice checked (F11).
- [x] `docs/plans/announcement.md` final draft (2026-10-20, rel-gen2, 203933c): `{{VERSION}}` and `{{RELEASE_URL}}` kept
      for the tag, screenshots, the bitstream note (attached with `BITSTREAMS.md`) and the tested-on line filled; the clip
      and the other maintainer decisions are the checklist at the top of that file.

## Tag and after

- [ ] `git tag -a v0.3.0 -m "fnirsi-1013d-mcp 0.3.0"`, `git push origin v0.3.0`, `release.yml` green.
- [ ] FEL check of the attached image (`docs/releasing.md` section 4): read-back identical, `#VER fw=0.3.0`, build hash
      equals `git rev-parse --short=8 v0.3.0`; noted in `docs/hardware-status.md`.
- [ ] `docs/releasing.md` section 7: the two open boxes ticked.
- [ ] Announcement posted once (EEVblog thread, the repository, the subreddits in the draft).
