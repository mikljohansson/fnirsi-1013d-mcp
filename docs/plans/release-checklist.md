# Pre-tag checklist, v0.3.0

The bench session ticks this before `git tag -a v0.3.0`. Procedure and commands: `docs/releasing.md` section 2;
background and the findings F1-F12: `docs/plans/release-readiness.md`; gate: `docs/plans/roadmap.md` section 5.
Record each tick with the date and the commit or log line that proves it. Untick a box if its input changes
afterwards (a new firmware commit invalidates every box from "Versions" down).

Paperwork pass by rel-gen2 on 2026-10-20 (branch `rel-gen2` from 876700a): the boxes below that it ticked name that
branch's commit; the maintainer's items and the hardware items are left open.

**State 2026-11-07 17:00: v0.3.0 is tagged and published as a pre-release (ab93c0d), every check closed; only the announcement is left, the owner's.** The checks after the tag are in `docs/hardware-status.md`, 13:50 to 16:54.

## Content

- [x] Every epic of `docs/plans/roadmap.md` sections 1-3 merged and benched, or removed from the list by the maintainer. (2026-11-07, owner: closed or moved to after 0.3.0; phosphor fast capture and the remaining measurements after the release.)
      (The roadmap's state line names 0x153F for the release since 2026-11-07; sections 1-4 are still the 2026-10-20 text.)
- [x] Maintainer verdicts in `docs/hardware-status.md` for the built-but-unbenched items (calibration, phosphor, generator rows, (2026-11-07, `docs/hardware-status.md` 14:30-15:00.)
      USB long idle, UI steps), or the item named under "Known issues" in the release notes.

## FPGA bitstreams

Rewritten 2026-10-26 (docs-rel153d) for the release bitstream 0x153D (the gated build fd0be7a2 of fpga-d3gate). The
0x153B boxes of 2026-10-20 are history: `fpga/al3/dist/BITSTREAMS.md` keeps 0x153B's bench record.

**Owner decision 2026-11-04 (docs-153e): the release bitstream is 0x153E**, the 0x153D design with the ADC encodes
driven from PLL outputs through the pin registers (`docs/hardware-status.md` 2026-11-04 10:23-10:38 "enc0" and 11:1x-11:33
"session end and decisions"). The 0x153D boxes below stay as the record of what 0x153D proved; 0x153D remains attached
as `supported` (benched good with fw 0.2.105, refused by the 0.3.0 firmware).

**2026-11-07 (docs-153f): the release bitstream is 0x153F**, 0x153E with both ADC cores of a chip captured at one instant
(the core-order fix; `docs/hardware-status.md` 2026-11-06 16:21 onward, `fpga/al3/README.md` section 22). The 0x153E boxes
below stay as the record of what 0x153E proved; 0x153E was never released and stays attached as `supported`, refused by
the 0.3.0 firmware. The 0x153F boxes come first.

- [x] 0x153F built by fpga-153f and pinned (2026-11-07, merge 7f4ed8e): committed `fpga/al3/src` (roll ea38475),
      `tb/run.sh` ALL PASS on seeds 12345 and 4242, every clock closed (clk_200MHz 4.789 ns), raw sha256
      `e581a67fcf9d8a21ba01b212212ec17d92c9f1d98e5c3ad082cd79f8d930ece0` and padded
      `d255ca3824786d80d507c94e7c21b2497e035c98753b5db1d2f395ab5052d5bc` in `fpga/al3/dist/BITSTREAMS.md` and
      `manifest.json`. Proof: `docs/hardware-status.md` 2026-11-07 05:45 and the dist rows.
- [x] `fpga/al3/dist`: 0x153F `status: recommended` (2026-11-07, abd988a, after the bench), `min_firmware` = the 0.3.0
      firmware; 0x153E and 0x153D `supported`; `python3 -I tools/bitstreams.py check --release` exits 0 (2026-11-07,
      docs-153f: "13 bitstreams consistent"). Run again at the release commit.
- [x] 0x153F flashed on the maintainer's unit (owner at the bench, 2026-11-07 09:43-09:50): SD 700-710 backed up twice,
      pre-write double read equal (2782e1b6..., the 0x153E image), verified on the first pass, independently read back
      equal to the padded image's hash d255ca38...; `docs/hardware-status.md` entry of that time.
- [x] 0x153F benched on the firmware that expects it (2026-11-07, fw 1d36d9b5 of 53da95a loaded over FEL,
      `docs/hardware-status.md` 09:52-11:33): no mismatch notice, the first-run screen on the 0x153E calibration, the wizard
      trained stage 3 on both chips (0x06DB), the designed core delay at twelve rates on both channels (8 of 8 each with
      `core_delay_fold.py`; divider 0 by votes, a shallow minimum), capture self-check 0 flagged of 7562, CH2 code errors 0 %
      at 2 MSa/s, counter 1000.00 Hz at 20 kSa/s Filter Off read past the flag, `fnirsi1013d-battery` 201 PASS, 0 FAIL, 1 SKIP
      (the logger group among them). Encode check: 3 captures re-taken of 42926, all recovered, 0 accepted bad.
- [x] Owner accepted 0x153F for 0.3.0 (2026-11-07, recorded in abd988a and `BITSTREAMS.md`) with the bench list's sweep limit
      missed: peak over the CH2 offset sweep 80 minutes after power-on A 5.8 %, B 1.0 % against 0x153E's A 5.2 %, B 0.65 %
      (`docs/hardware-status.md` 10:52-11:33), for the right core order. The release notes list it as a known issue.
- [x] 30-minute soak on 0x153F. (0x153E's two: 2026-11-06 14:20 with 4 link drops of 0.6 s, 15:50 with 0; cause not found.) (2026-11-07 11:59-12:30, fw 1d36d9b5 from the card: 17897 requests, 0 drops.)
      Update the known issue "USB link drops" in the release notes and the announcement with the result.

- [x] 0x153E built by fpga-153e and pinned (2026-11-04, merge 0cd4768): committed `fpga/al3/src` (roll da645d0),
      `tb/run.sh` ALL PASS on seeds 12345 and 4242, every clock closed (clk_200MHz 4.802 ns), raw sha256
      `831dc3465e7bdddf67bfd263755f00f7fc95fcd5e487a9ca52ec49e2531602e1` and padded
      `2782e1b6246d6f1bf7603a2d011ff550e01d47f71953ccce4b6c25069f74d9d8` in `fpga/al3/dist/BITSTREAMS.md` and
      `manifest.json`. Proof: the fpga-153e handoff in `fpga/al3/AGENTS.md` and the dist rows.
- [x] `fpga/al3/dist`: 0x153E `status: recommended` (2026-11-06, after the bench), `min_firmware` = the 0.3.0 firmware;
      0x153D `supported`; `python3 -I tools/bitstreams.py check --release` exits 0 (12 bitstreams consistent).
- [x] 0x153E flashed on the maintainer's unit (owner at the bench, 2026-11-06 14:00-14:04): pre-write double read equal
      (aa252fb7...), written (the first pass failed its verify, the second verified), independently read back equal to
      the padded image's hash 2782e1b6...; `docs/hardware-status.md` entry of that time.
- [x] 0x153E benched on the firmware that expects it (closed 2026-11-07, never released; superseded by the 0x153F boxes above; left open for its soak, whose
      first run had the 4 drops): cold boot with no mismatch notice, encode self-check 0 bad, capture
      self-check 0 flagged, the firmware asks for a calibration and the wizard completes (stages trained), CH2 code
      errors at 2 and 200 MSa/s with the sweep (`fpga/al3/tools`), counter, a logger run, soak. Proof: the hardware-status
      entries; the release notes' `{{0x153E bench}}` line filled from them.

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
- [x] Not repeated on the gated 0x153D: the logger tiers and roll mode (2026-11-07: run on 0x153F: logger and device Bode in the battery, roll by the owner.) (benched on the first, full-rate 0x153D build
      2026-10-22 08:47-09:02, whose logger logic the gated build keeps) and the device Bode sweep (09:16-09:49, same
      build). Re-run on the release candidate, or accept the inference.
- [x] Maintainer decision recorded (2026-11-07: accepted; Atlan4's FPGA source taken as GPL-3.0 from his repository's top-level licence): bitstreams are committed under `fpga/al3/dist/` and attached to releases
      (`docs/releasing.md` section 5).

## Versions and notes

- [x] `firmware/scope/pc_cmds.h`: `#define PC_FW_VERSION "0.3.0" ...` (the one quoted literal). Check in the same pass the FPGA
      version the firmware expects (`PC_FW_FPGA_VERSION` = 0x153F, also the mcp and protocol docs and the simulator) against
      `fpga/al3/dist/manifest.json` (the `recommended` row).
- [x] `mcp/pyproject.toml`: `version = "0.3.0"`.
- [x] `CHANGELOG.md`: `## [Unreleased]` first and empty, `## [0.3.0] - YYYY-MM-DD` below it and non-empty; the obsolete
      "Plan for the first tag" block and "Nothing has been tagged yet" removed; the 0.2.46-0.2.50 sub-sections in order.
      Applied in the release commit (2026-11-07); the draft was `docs/release-notes/changelog-0.3.0-draft.md` (rel-gen2, 517f252;
      refreshed up to fa8377b by docs-rel153d, 2026-10-26, and up to abd988a by docs-153f, 2026-11-07); delete that file in
      the release commit.
- [x] `docs/release-notes/0.3.0.md` (re-ticked 2026-11-07, docs-153f: the 0x153F bench record and known issues from the bench log): every `{{placeholder}}` filled (screenshots, tested-on list, known issues), each fact
      checked against `docs/hardware-status.md` once more, user-facing wording only (2026-10-20, rel-gen2, c0ea0c1;
      `mkdocs build --strict` passes). Refreshed for 0x153D and fw 0.2.105 by docs-rel153d on 2026-10-26 (the TODO
      resolved with 0x153D's bench record). Re-check the tested-on line after the release-candidate bench.
- [x] `python .github/scripts/release_meta.py --tag v0.3.0 --notes notes.md` exits 0; `notes.md` read as a visitor would.
      Dry-run 2026-10-20 (rel-gen2) on a scratch tree with both versions at 0.3.0 and the draft CHANGELOG section applied:
      `--tag v0.3.0`, `--tag v0.3.0-rc1` and `--check-changelog` exit 0. Run it on the release commit itself.

## Build and CI

- [x] `release: v0.3.0` commit on main, CI green: firmware (GCC 10, GCC 13, PC_DEBUG, host tests), mcp on Linux and (9bf1ce2 failed one job, mcp windows py3.12, a simulator clock; ab93c0d, the tagged commit, green.)
      Windows, **wheel** on Linux and Windows, protocol vectors, CHANGELOG structure, bitstream check.
- [x] Local `make gcc10` sha256 of that commit equals the sha256 the CI firmware job prints (reproducibility). (9bf1ce2: 727745b0 both; the tagged ab93c0d: ac278214 from CI, the same image run on the unit.)
- [x] `python tools/make_release_sd_package.py build --scope <image> --parts parts` run against the real Atlan4 download (2026-11-07: not by hand; release.yml's publish job ran the zip's tools against the real download on rc1 and v0.3.0.)
      the day before the tag (F7).

## SD package and bench

- [x] (2026-11-07 13:50-14:10, a 16 GB card, `docs/hardware-status.md`; the owner saw the boot, the identity came over USB and from the report file) SD parts built from the CI image of the release commit with the tools zip, written to a spare card prepared as
      `docs/guide/install.md` says (FAT32 partition at sector 16384, 8 MiB) with `make_sd_image.py write`; the same
      command on a card partitioned at 2048 refuses and names the start it needs; the scope boots the prepared card: boot menu, PECO preselected, `#VER fw=0.3.0`, the Diagnostics
      identity line shows the release commit.
- [x] (2026-11-07 14:0x: fw 0.3.0 ac278214 from the maintainer's card after a power cycle.) A persisted release candidate boots from the maintainer's card (partition at 8192; fw 0.2.101+ writes up to the partition).
      Unticked 2026-10-26 (docs-rel153d): the input changed. History: fw 0.2.101 / 0.2.102 persisted and cold-booted
      2026-10-20 19:22-19:29; fw 0.2.105 ba1a1283 persisted 2026-10-22 09:08 and booted from the card; fw 0.2.105 c681efa4
      (07c2bbf) on 0x153D persisted 2026-10-22 19:52, 1597 sectors read back identical (a cold boot of it is not logged).
      To do for the 0.3.0 image.
- [x] 30-minute soak with the host polling passes. Unticked 2026-10-26 (docs-rel153d): the 30-minute run (2026-10-20 (Ticked 2026-11-07 on fw 1d36d9b5, the same code as 0.3.0 but the version literal, see above.)
      21:26-21:56, 899 status polls, 15 health reads, 15 captures, 0 errors) was on fw 0.2.102 / 0x153B. On 0x153D only
      shorter runs exist: 5 minutes on the first 0x153D build (2026-10-22 10:16-10:22, 0 errors) and 301 s on the gated
      0x153D with fw 0.2.105 c681efa4 (18:56-19:02, 150 status, 4 health, 4 captures, 0 errors). Run 30 minutes on the
      0.3.0 image.
- [x] The long-idle check passes (roadmap section 5 item 4). (2026-11-07 14:54-16:54 on fw 0.3.0: 2 h with the port closed, the scope answers after it, 0 failures.)
- [x] The hardware battery passes on the 0.3.0 image (`fnirsi1013d-battery --port <port>`, docs/releasing.md section 4
      step 6): exit 0, or every FAIL a known issue in the release notes; `battery.md` pasted into
      docs/hardware-status.md. The loopback group with CH1 on the post (`--groups loopback`) passes (2026-11-07: 7 of 15 checks pass, the 8 others traced to the test, not the unit, `docs/hardware-status.md` 14:14; test fixes queued for 0.3.1). On 0x153F with the
      bench image (fw 1d36d9b5, 2026-11-07 10:34-10:38): 201 PASS, 0 FAIL, 1 SKIP, the `coreorder` group and the Filter Off
      steps that failed on 0x153E included; to repeat on the 0.3.0 image.
- [x] (2026-11-07: the first-run screen on fw 0.3.0 on the spare card, seen by the owner; the wizard on the same code, the bench image.) First-run screen and calibration wizard on the release candidate with the recommended bitstream (the wizard passed
      on fw 0.2.105 with the gated 0x153D on 2026-10-22 17:0x-17:21 and 18:27-18:29, and on 0x153F with the bench image
      on 2026-11-07 09:52-10:31, the first-run screen included; the 0.3.0 image remains).

## Dry run

- [x] `v0.3.0-rc1` tag pushed on the release commit; `release.yml` green (check, ci, publish). (ab93c0d.)
- [x] The rc release page checked: notes, the assets (`fw.bin`, `sdtools.zip`, wheel, sdist, the bitstreams and their
      `-flash1m` images, `BITSTREAMS.md`, `SHA256SUMS`), marked pre-release; `sha256sum -c SHA256SUMS` on a download. The
      notes' screenshots are raw URLs pinned to commit 68b733b (on origin/main), so they show on the rc page too.
- [x] The rc release and tag deleted.

## Presentation

- [x] Screenshots taken with `scope_screenshot` (through `tools/screenshots.py`, which writes `docs/screenshots/`, not
      `docs/img/`) and committed: `scope.png` (scope view with cards and the MCP chip, fw 0.2.96, 5d37c4f),
      `spectrum.png` (eb645ec), `diagnostics.png` (Status page all OK on fw 0.2.102 from the card, 49c295f, 19:29), and in place of a
      host `bode.png` the on-device `frequency-response-sweep.png` (edd6f85). The same images in the release notes
      (rel-gen2, c0ea0c1).
- [x] README screenshot block un-commented and on `docs/screenshots/*.png` (done in the README rewrite; docs-rel153d
      2026-10-27 added acq-menu.png and acq-menu-roll.png and replaced the stale "What's next" list).
- [x] README links checked (every relative link resolves on GitHub): 2026-10-27 on docs-rel153d, 24 relative links, all
      tracked files or directories. Re-check if the README changes before the tag.
- [x] Credits and AI attribution wording confirmed by the maintainer (README "Credits", F12), 2026-11-07, unchanged.
- [x] GitHub repository description, topics, Discussions on if the announcement uses them, issue templates (F11), 2026-11-07: description kept, ten topics, Discussions on, bug and unit-report templates.
- [x] Vendored `research/tools/vendor/capstone/lib/capstone.dll` licence notice checked (F11), 2026-11-07: not in the tree since eb55f9d or in any release; it remains in git history only (6c7327c), the owner closes it with no notice added.
- [x] `docs/plans/announcement.md` final draft (2026-10-20, rel-gen2, 203933c): `{{VERSION}}` and `{{RELEASE_URL}}` kept
      for the tag, screenshots, the bitstream note (attached with `BITSTREAMS.md`) and the tested-on line filled; the clip
      and the other maintainer decisions are the checklist at the top of that file. Refreshed for 0x153D, fw 0.2.105 and
      41 / 39 tools by docs-rel153d (2026-10-27); screenshots pinned to 68b733b. Refreshed for 0x153F by docs-153f
      (2026-11-07).

## Tag and after

- [x] `git tag -a v0.3.0 -m "fnirsi-1013d-mcp 0.3.0"`, `git push origin v0.3.0`, `release.yml` green. (2026-11-07, ab93c0d.)
- [x] FEL check of the attached image (`docs/releasing.md` section 4): read-back identical, `#VER fw=0.3.0 (2026-11-07, on the rc asset, the same image as the tag's.)`, build hash
      equals `git rev-parse --short=8 v0.3.0`; noted in `docs/hardware-status.md`.
- [x] `docs/releasing.md` section 7: the two open boxes ticked (2026-11-07: both shown by the rc1 and v0.3.0 runs of `release.yml`).
- [ ] Announcement posted once (EEVblog thread, the repository, the subreddits in the draft).
