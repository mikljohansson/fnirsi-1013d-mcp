# Pre-tag checklist, v0.3.0

The bench session ticks this before `git tag -a v0.3.0`. Procedure and commands: `docs/releasing.md` section 2;
background and the findings F1-F12: `docs/plans/release-readiness.md`; gate: `docs/plans/roadmap.md` section 5.
Record each tick with the date and the commit or log line that proves it. Untick a box if its input changes
afterwards (a new firmware commit invalidates every box from "Versions" down).

Paperwork pass by rel-gen2 on 2026-10-20 (branch `rel-gen2` from 876700a): the boxes below that it ticked name that
branch's commit; the maintainer's items and the hardware items are left open.

## Content

- [ ] Every epic of `docs/plans/roadmap.md` sections 1-3 merged and benched, or removed from the list by the maintainer.
      (The roadmap's state line names fw 0.2.105 / 0x153D since 2026-10-26; sections 1-4 are still the 2026-10-20 text.)
- [ ] Maintainer verdicts in `docs/hardware-status.md` for the built-but-unbenched items (calibration, phosphor, generator rows,
      USB long idle, UI steps), or the item named under "Known issues" in the release notes.

## FPGA bitstreams

Rewritten 2026-10-26 (docs-rel153d) for the release bitstream 0x153D (the gated build fd0be7a2 of fpga-d3gate). The
0x153B boxes of 2026-10-20 are history: `fpga/al3/dist/BITSTREAMS.md` keeps 0x153B's bench record.

- [x] 0x153D pinned by fpga-d3gate (merge f83f80f, 2026-10-22 15:2x): built from a committed `fpga/al3/src` (source
      e3a23c2, roll h1, every clock closed), `tb/run.sh` 18 steps ALL PASS on seeds 12345 and 4242, raw `fd0be7a2...e96a`
      and padded `aa252fb7...63cc` in `fpga/al3/dist/BITSTREAMS.md` and `manifest.json` (`fpga/al3/AGENTS.md` fpga-d3gate
      handoff).
- [x] 0x153D flashed on the maintainer's unit 2026-10-22 17:01 (pre-write double read = the 0x153C image 07393bf5..,
      verified and independently read back aa252fb7..) and benched 17:03-19:02 on fw 0.2.105 (`docs/hardware-status.md`
      2026-10-22 17:01, 17:03-17:21, 18:01, 18:27-18:33, 18:56-19:02): wizard trained 0x06E3; CH2 code-error A/B
      2 MSa/s 0 % on both cores, 200 MSa/s A 3.32 % / B 0.37 %; counter gating as designed; 301 s soak 0 errors.
- [x] Owner accepted the gated 0x153D for 0.3.0 on 2026-10-22 18:56 with CH2 A at 3.32 % at 200 MSa/s (errors at the
      0x5F/0x60 carry; the 0x153C run of 11:36 not like-for-like) (`docs/hardware-status.md` 18:27-18:33,
      `docs/plans/task-queue.md` fpga-d3gate entry). The release notes list it as a known issue.
- [x] `fpga/al3/dist`: 0x153D `status: recommended` with its bench record, `min_firmware` 0.2.105 (docs-rel153d,
      2026-10-26).
- [x] Status of the older images decided, and the "Use the recommended image" paragraph of `BITSTREAMS.md` true
      (docs-rel153d, 2026-10-26): 0x1533-0x1535, 0x153A, 0x153B, 0x153C `supported` (benched good with the firmware named
      in their row, attached as rollback targets for that firmware; 0.2.105 refuses them), 0x1536 `unbenched`,
      0x1537-0x1539 `withdrawn` (not attached). The 0x1538 / 0x1539 rows read withdrawn like the manifest.
- [x] `python3 -I tools/bitstreams.py check --release` exits 0 (2026-10-26, docs-rel153d: "11 bitstreams consistent").
      Run again at the release commit.
- [ ] Not repeated on the gated 0x153D: the logger tiers and roll mode (benched on the first, full-rate 0x153D build
      2026-10-22 08:47-09:02, whose logger logic the gated build keeps) and the device Bode sweep (09:16-09:49, same
      build). Re-run on the release candidate, or accept the inference.
- [ ] Maintainer decision recorded: bitstreams are committed under `fpga/al3/dist/` and attached to releases
      (`docs/releasing.md` section 5).

## Versions and notes

- [ ] `firmware/scope/pc_cmds.h`: `#define PC_FW_VERSION "0.3.0" ...` (the one quoted literal).
- [ ] `mcp/pyproject.toml`: `version = "0.3.0"`.
- [ ] `CHANGELOG.md`: `## [Unreleased]` first and empty, `## [0.3.0] - YYYY-MM-DD` below it and non-empty; the obsolete
      "Plan for the first tag" block and "Nothing has been tagged yet" removed; the 0.2.46-0.2.50 sub-sections in order.
      The section text and the steps are ready in `docs/release-notes/changelog-0.3.0-draft.md` (rel-gen2, 517f252;
      refreshed up to fa8377b by docs-rel153d, 2026-10-26); delete that file in the release commit.
- [x] `docs/release-notes/0.3.0.md`: every `{{placeholder}}` filled (screenshots, tested-on list, known issues), each fact
      checked against `docs/hardware-status.md` once more, user-facing wording only (2026-10-20, rel-gen2, c0ea0c1;
      `mkdocs build --strict` passes). Refreshed for 0x153D and fw 0.2.105 by docs-rel153d on 2026-10-26 (the TODO
      resolved with 0x153D's bench record). Re-check the tested-on line after the release-candidate bench.
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
- [ ] A persisted release candidate boots from the maintainer's card (partition at 8192; fw 0.2.101+ writes up to the partition).
      Unticked 2026-10-26 (docs-rel153d): the input changed. History: fw 0.2.101 / 0.2.102 persisted and cold-booted
      2026-10-20 19:22-19:29; fw 0.2.105 ba1a1283 persisted 2026-10-22 09:08 and booted from the card; fw 0.2.105 c681efa4
      (07c2bbf) on 0x153D persisted 2026-10-22 19:52, 1597 sectors read back identical (a cold boot of it is not logged).
      To do for the 0.3.0 image.
- [ ] 30-minute soak with the host polling passes. Unticked 2026-10-26 (docs-rel153d): the 30-minute run (2026-10-20
      21:26-21:56, 899 status polls, 15 health reads, 15 captures, 0 errors) was on fw 0.2.102 / 0x153B. On 0x153D only
      shorter runs exist: 5 minutes on the first 0x153D build (2026-10-22 10:16-10:22, 0 errors) and 301 s on the gated
      0x153D with fw 0.2.105 c681efa4 (18:56-19:02, 150 status, 4 health, 4 captures, 0 errors). Run 30 minutes on the
      0.3.0 image.
- [ ] The long-idle check passes (roadmap section 5 item 4).
- [ ] First-run screen and calibration wizard on the release candidate with the recommended bitstream (the wizard passed
      on fw 0.2.105 with the gated 0x153D on 2026-10-22 17:0x-17:21 and 18:27-18:29; the first-run screen and the 0.3.0
      image remain).

## Dry run

- [ ] `v0.3.0-rc1` tag pushed on the release commit; `release.yml` green (check, ci, publish).
- [ ] The rc release page checked: notes, the assets (`fw.bin`, `sdtools.zip`, wheel, sdist, the bitstreams and their
      `-flash1m` images, `BITSTREAMS.md`, `SHA256SUMS`), marked pre-release; `sha256sum -c SHA256SUMS` on a download. The
      notes' screenshots are raw URLs pinned to commit 68b733b (on origin/main), so they show on the rc page too.
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
