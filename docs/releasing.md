# Releasing

How a release of fnirsi-1013d-mcp is cut, what the GitHub workflows do, how to check a release image
on the scope, and why Atlan4's bootloader and updater are fetched rather than attached.

Workflows: `.github/workflows/ci.yml`, `.github/workflows/release.yml`, the composite action
`.github/actions/firmware-build/action.yml` and `.github/scripts/release_meta.py`. History goes in
`CHANGELOG.md`.

## 1. What CI checks on every push and pull request to main

| job | runner | what must pass |
|---|---|---|
| firmware | ubuntu-24.04 | see the list below |
| mcp | ubuntu-24.04 and windows-latest, Python 3.12 and 3.13 | `pip install -e mcp[dev]`, then ruff with the selection from `mcp/AGENTS.md`, then pytest |
| wheel | ubuntu-24.04 and windows-latest, Python 3.12 | `python -m build mcp` exactly as `release.yml` builds it, then `.github/scripts/wheel_smoke.py`: the wheel installed into a fresh venv outside the checkout, `fnirsi1013d-mcp --list-tools`, an MCP stdio session on the simulator, files under `FNIRSI_MCP_HOME`, clear errors for the checkout-only inputs |
| docs-lint | ubuntu-24.04 | `docs/protocol-vectors.json` against the Python checker and the C checker; `CHANGELOG.md` structure; `tools/bitstreams.py check` (the committed FPGA bitstreams against `fpga/al3/dist/manifest.json` and `BITSTREAMS.md`) |

The firmware job runs these steps in order:

1. Install the apt toolchain (GCC 13.2.1).
2. Unpack Atlan4's GCC 10.3.1 with `firmware/tools/fetch_gcc10.sh`. It is cached and keyed on the
   script's hash.
3. Run `make gcc10`, then `make`, then `make PC_DEBUG=1 BUILD=build-dbg`.
4. Run `make -C host_tests test`, which includes `check_vectors.py`.

The job uploads `build-gcc10-pc/fnirsi_1013d.bin` and its `.sha256` as the `firmware-image` artifact.
The step summary lists the size and sha256 of each of the three builds.

Until 2026-10-18 the job also fetched Atlan4's v1.00o5 binary and compared it with a build of this tree,
which proved the two byte-identical. The owner dropped that rule to allow dead-code removal. Fw 0.2.47
removed the comparison, the `PC_PROTOCOL=0` build, `make package` and the download; the CI logs up to that date
hold the proof. SD packages are made only by `tools/make_release_sd_package.py build` (section 3).

Warnings are handled exactly as the repository already handles them. The firmware builds keep the
upstream warnings and do not use `-Werror`. The host tests build with `-Werror`, as their Makefile
says. The warning-list comparison in `firmware/AGENTS.md` stays a manual review step.

The release image is reproducible (fw 0.2.47, `firmware/README.md` "Reproducible images"). The image depends
only on the source, the toolchain and the commit. The Makefile reads the 8-digit commit hash (`PC_FW_BUILD`)
and the commit time (`SOURCE_DATE_EPOCH`, UTC) from git, never the wall clock or the checkout path. They enter
the image as ten bytes of data in `build_id.c`, which is linked last, and the source directory is mapped out of
the debug info. So the `.bin` and the `.elf` are the same from every directory, including a git worktree
(the Makefile translates a worktree's Windows gitdir for WSL). To rebuild a release image locally:

```
git checkout vX.Y.Z
cd firmware/scope
make gcc10
sha256sum build-gcc10-pc/fnirsi_1013d.bin      # equals the release's SHA256SUMS line
```

Where git cannot be read at all, the build stops and asks for
`PC_FW_BUILD=<git rev-parse --short=8 HEAD> SOURCE_DATE_EPOCH=<git log -1 --format=%ct>`.

To check reproducibility, build the same commit in two clones at different paths and compare the sha256 of
`build-gcc10-pc/fnirsi_1013d.bin` and of the `.elf`. The firmware job prints the image sha256 in its log and as a
notice, and a local `make gcc10` of the same commit must match it. A build from uncommitted changes carries its
parent commit's identity but different code, so its hash differs. Images before 0.2.47 were not reproducible
across checkout paths (the 0.2.44 images in `docs/hardware-status.md`). The release workflow builds only
`make gcc10`; `make gcc10-nommu` (MMU off, version suffix `-nommu`) is a local A/B build.

## 2. Cutting a release

The firmware and the mcp package share one version. The first tag is planned as v0.3.0
(`CHANGELOG.md`, `docs/plans/roadmap.md` section 5). The coordinator ticks `docs/plans/release-checklist.md`
on the way.

1. Bump the version in two places:
   - `firmware/scope/pc_cmds.h`: the one line `#define PC_FW_VERSION "X.Y.Z" PC_FW_VERSION_MAP PC_FW_VERSION_MMU`.
     Change only the quoted literal; the `-m1` and `-nommu` suffixes are macros of their own builds.
   - `mcp/pyproject.toml`: `version = "X.Y.Z"`.
2. `CHANGELOG.md` stays the developer history. Add a section `## [X.Y.Z] - YYYY-MM-DD` under
   `## [Unreleased]` and leave `## [Unreleased]` in place and empty. The section must not be empty (the check
   below), but it is not the release notes, so it can be short: what the release contains, by component, with
   the commit citations, and where the per-build history is. Do not move the whole dev diary into it.
3. **Release notes** are a separate, user-facing file, `docs/release-notes/X.Y.Z.md`; its text is the body of
   the GitHub release. Write it for someone who owns a 1013D and has never seen the repository: what it is,
   the highlights, the install steps, the caveats before flashing, known issues, credits. No protocol rows, no
   agent names, no build-by-build history. Start from the previous release's file (for 0.3.0 the draft is
   there, made from `docs/plans/announcement.md` and the CHANGELOG since 0.2.25). Every `{{placeholder}}`
   must be filled; `release_meta.py` refuses a file that still has one. The job appends the file list and
   `SHA256SUMS` to it.
4. **FPGA bitstreams.** The release attaches the images committed in `fpga/al3/dist/`
   (`fpga/al3/dist/BITSTREAMS.md`, "Provenance", says why they are committed and not built in CI). For a new
   bitstream version, before the release commit:
   - build it from a committed `fpga/al3/src` (`SRC_REV=<commit> ./build.sh`, `fpga/al3/TD_SETUP.md`), flash it
     on the owner's unit and bench it (`docs/hardware-status.md`);
   - copy the raw `FPGA.bin` to `fpga/al3/dist/al3-0x15xx.bin` and add an entry to `manifest.json`: size,
     sha256, the sha256 of the image padded with 0xFF to 1 MiB, the full source commit, the first firmware that
     uses it, status and the bench record. Exactly one entry is `recommended`; one that must not be used is
     `withdrawn` (kept in git, not attached);
   - add its rows to `BITSTREAMS.md` (the "Which one to use" table and the hash table);
   - run `python3 -I tools/bitstreams.py check --release`. It checks every file against the manifest (size,
     hash, the AL3A10 header, the padded hash), that `BITSTREAMS.md` names every hash, and for a release that
     one benched image is recommended and no `{{placeholder}}` is left.
5. Before committing, run the same checks as CI:

   ```
   wsl -d Ubuntu -- bash -lc "cd /mnt/d/src/fnirsi-1013d-ai/firmware/scope && make gcc10 && make && make -C host_tests test"
   cd mcp && .venv/Scripts/python -m pytest -q
   python .github/scripts/wheel_smoke.py
   python -I tools/bitstreams.py check --release
   ```

   Then check the tag metadata. Exit 0 means the tag will be accepted, and `notes.md` is what the release
   page will show above the file list:

   ```
   python .github/scripts/release_meta.py --tag vX.Y.Z --notes notes.md
   ```

6. Commit on main with the subject `release: vX.Y.Z` and push. Wait for CI to pass.
7. Dry run on that commit (next section). Then tag it and push the tag:

   ```
   git tag -a vX.Y.Z -m "fnirsi-1013d-mcp X.Y.Z"
   git push origin vX.Y.Z
   ```

### Dry run: a `-rcN` tag

`release.yml` runs on every tag that matches `v*.*.*`, and `release_meta.py` accepts `vX.Y.Z` and
`vX.Y.Z-rcN`. An rc tag goes through the same jobs on the same commit and with the same checks against
`X.Y.Z` (versions, CHANGELOG section, release notes, bitstreams), and its release is always a pre-release
titled `fnirsi-1013d-mcp X.Y.Z-rcN`. Asset names carry `X.Y.Z`, as they will on the real release. This is the
way to run the workflow before the real tag, in the real repository with its real permissions; a fork works
too but tests a different repository's settings.

```
git tag -a v0.3.0-rc1 -m "fnirsi-1013d-mcp 0.3.0 dry run"
git push origin v0.3.0-rc1
gh run watch "$(gh run list --workflow release.yml --limit 1 --json databaseId -q '.[0].databaseId')" --exit-status
gh release view v0.3.0-rc1                      # the assets, the notes, marked pre-release
gh release download v0.3.0-rc1 -D rc1 && (cd rc1 && sha256sum -c SHA256SUMS)
```

Check the release page as a visitor would: the notes, every asset, `BITSTREAMS.md`. Then remove the dry run,
release first, then the tag:

```
gh release delete v0.3.0-rc1 --yes
git push origin :refs/tags/v0.3.0-rc1 && git tag -d v0.3.0-rc1
```

An rc that failed costs nothing: fix on main, tag `-rc2`. A failed run on the real tag costs a patch version
(below), which is why the rc comes first.

### What release.yml does

1. **check**: `release_meta.py` fails the run if any of these hold:
   - the tag is not `vMAJOR.MINOR.PATCH` or `vMAJOR.MINOR.PATCH-rcN`;
   - `PC_FW_VERSION` or the mcp version differs from `X.Y.Z`;
   - the CHANGELOG section `[X.Y.Z]` is missing or empty;
   - `docs/release-notes/X.Y.Z.md` is missing, empty or still has a `{{placeholder}}`.

   Then `tools/bitstreams.py check --release` fails it unless the committed bitstreams match the manifest,
   exactly one benched image is `recommended` and `BITSTREAMS.md` has no placeholder.
2. **ci**: the whole of `ci.yml` runs as a reusable workflow on the tag commit, including the `wheel` job.
3. **publish** first checks that the image is the one CI built for this tag. The artifact's sha256 must
   still match, and the `#VER` literal in the image must report the version. The job then attaches
   these files to a GitHub release:

| file | content |
|---|---|
| `fnirsi_1013d-fw-X.Y.Z.bin` | the scope image (eGON.EXE, GCC 10 protocol build) for FEL or SD sector 750 |
| `fnirsi_1013d-sdtools-X.Y.Z.zip` | `make_release_sd_package.py`, `make_sd_image.py`, `mksunxi.py` |
| `fnirsi1013d-X.Y.Z-py3-none-any.whl`, `fnirsi1013d-X.Y.Z.tar.gz` | the mcp package (`python -m build mcp`, the same build the `wheel` job tested) |
| `fnirsi_1013d-fpga-al3-0x15xx.bin` | each bitstream in `fpga/al3/dist` except `withdrawn` ones, as bitgen wrote it |
| `fnirsi_1013d-fpga-al3-0x15xx-flash1m.bin` | the same padded with 0xFF to the 1 MiB config flash, the file users write |
| `BITSTREAMS.md` | which bitstream to use, firmware versions, how to flash, rollback |
| `SHA256SUMS` | sha256 of every file above, also printed in the release notes |

Before it publishes, the job runs the SD tools from the zip on a clean runner. They download Atlan4's
package and build an SD package with the new image, which tests the path users take.

Tags with major version 0, and every `-rcN` tag, are marked as pre-release. The release is created with
`gh release create --verify-tag`, so the tag must already exist on GitHub.

If a run fails, fix the cause on main and delete the tag locally and remotely. Then tag the fixed commit.
Do not move a tag after a release has been published from it. Bump the patch version instead.

## 3. Using a release

- **RAM load over FEL**, for development, with nothing written:
  `fnirsi1013d-flash --image fnirsi_1013d-fw-X.Y.Z.bin --sha256 <hash from SHA256SUMS>`. Or use sunxi-fel
  directly, as in section 4.
- **Persist to the card** from a running protocol firmware: `fnirsi1013d-flash ... --persist` writes
  only sector 750 onwards, after a verified backup (`mcp/README.md`).
- **A new card, or one without the open bootloader**, needs Atlan4's bootloader v0.8 and updater as
  well. Unzip the SD tools and run:

  ```
  python3 make_release_sd_package.py build --scope fnirsi_1013d-fw-X.Y.Z.bin --scope-sha256 <hash> --parts parts
  python3 make_sd_image.py write --device /dev/sdX --bootloader parts/bootloader.bin \
          --updater parts/updater.bin --scope parts/scope.bin --boot-mode 4
  ```

  The first command downloads Atlan4's v1.00o5 zip from a pinned commit of
  github.com/Atlan4/Fnirsi1013D. It checks the sha256 of the scope image and the SD package in the zip,
  and checks that it can rebuild Atlan4's own package byte for byte. It then writes
  `fnirsi_1013d-fw-X.Y.Z-sdpackage.bin`, plus the three parts when `--parts` is given. With
  `--local <zip or directory>` it uses files you already have.

  `make_sd_image.py write` touches only the sectors of the parts you pass, and it backs up the card
  before writing a device. Do not `dd` the whole package from sector 16. That also zeroes sectors
  708-710, which hold calibration, settings and the display/boot-mode byte. Take a full card backup first.
- **FPGA bitstream** (AL3A10 only, needs a 3.3 V SPI programmer): the attached `BITSTREAMS.md` says which
  image to use, how to back up and write the config flash with `firmware/tools/spiflash_d2xx.py`, and how
  to roll back. Write the `-flash1m.bin` file.

## 4. Checking the attached hashes against a FEL load

This is done once per release, on the bench, by the owner or with the owner present (`AGENTS.md` rules).

1. Download the image and `SHA256SUMS` from the release, then run `sha256sum -c SHA256SUMS --ignore-missing`.
2. Put the scope in FEL. A running protocol firmware takes ENTER_FEL, which `fnirsi1013d-flash` sends.
   Otherwise use the boot menu's FEL button. Then attach 1f3a:efe8 to WSL.
3. Write the image without starting it, read it back, compare, and only then start it:

   ```
   sunxi-fel -p write 0x7FFFFFE0 fnirsi_1013d-fw-X.Y.Z.bin
   sunxi-fel read 0x7FFFFFE0 $(stat -c %s fnirsi_1013d-fw-X.Y.Z.bin) readback.bin
   sha256sum fnirsi_1013d-fw-X.Y.Z.bin readback.bin        # both must equal the SHA256SUMS line
   sunxi-fel exe 0x80000000
   ```

4. Check the running firmware's identity:
   - `#VER` must answer `fw=X.Y.Z`.
   - GET_INFO, `scope_info` and the diagnostics screen show the build hash. It must equal the first
     8 digits of the tag commit (`git rev-parse --short=8 vX.Y.Z`). The identity line reads
     `git <hash> of <commit time, UTC>`.
5. Note the result in `docs/hardware-status.md`: release, image sha256, FEL read-back identical, `#VER`.

## 5. GPL and Atlan4's binary-only parts

The scope application is GPL-3.0 (pecostm32, then Atlan4, then this repository), and its complete source
is in `firmware/scope`. A release that attaches the scope image therefore ships with its source.

The SD boot chain also needs two parts with no published source:

- Atlan4's **bootloader "BOOT fw v0.8"** at sector 16. The newest published source is v0.7
  (`firmware/README.md` section 3). v0.7 hangs on a custom FPGA bitstream, so v0.8 is required.
- the **updater v0.02v** at sector 80.

Both exist only as binaries inside Atlan4's v1.00o5 package. Atlan4 publishes them in a GPL project,
but redistributing a GPL binary obliges the distributor to offer the corresponding source. We cannot
offer source we do not have. So this repository never attaches them, and never attaches the full SD
package that contains them. Instead, `make_release_sd_package.py` fetches them from Atlan4's repository
on the user's machine. It pins the commit and checks sha256, so users get exactly the files Atlan4
published, from Atlan4. CI downloads nothing from Atlan4 except in the release job's SD tools test.

Our FPGA bitstreams are different: their source is `fpga/al3/src` in this repository (Atlan4's AL3
project, GPL-3.0 in his repository, plus our changes), each image names its source commit in
`fpga/al3/dist/manifest.json`, and the build is deterministic. So releases attach them. Atlan4's own 0x1532
is not attached; it is a download from his repository.

If Atlan4 publishes the bootloader v0.8 or updater source later:

1. Import it under `firmware/bootloader/`, with its licence and the upstream commit noted. Build it with
   a plain Makefile, like `firmware/scope`.
2. Prove a byte-identical rebuild of the binaries in the v1.00o5 package with a CI step that compares
   them with the pinned sha256 values.
3. Only then attach the bootloader, the updater or a full SD package to releases. Add a
   `fnirsi_1013d-fw-X.Y.Z-sdpackage.bin` step to `release.yml`. The SD tools zip can then become
   optional.
4. Update this section, `firmware/README.md` section 3 and `CHANGELOG.md`.

If the pinned download disappears, for example after a history rewrite in Atlan4's repository, the
release job's SD tools test fails. In that case, find the zip, verify the
two inner sha256 values in `tools/make_release_sd_package.py`, and update `ATLAN_COMMIT`. The inner
files must not change. If Atlan4 publishes a new package, that is a new reference and needs a deliberate
change.

## 6. The GitHub repository

The repository is public at https://github.com/mikljohansson/fnirsi-1013d-mcp, with remote `origin`.
It was created and published on 2026-10-18. Every push to main runs CI, and each commit gets its own
run. To start a run by hand, watch it and see a failing step:

```
gh workflow run ci.yml --ref main
gh run watch "$(gh run list --workflow ci.yml --limit 1 --json databaseId -q '.[0].databaseId')" --exit-status
gh run view <id> --log-failed
```

The default `GITHUB_TOKEN` permission of the repository is read
(`gh api repos/mikljohansson/fnirsi-1013d-mcp/actions/permissions/workflow`). That is intended. The
release job requests `contents: write` in `release.yml`, so no repository setting needs changing.

The repository contains a vendored third-party binary,
`research/tools/vendor/capstone/lib/capstone.dll`, of about 7 MB. Check its licence notice.

## 7. Checklist from the first runs on GitHub (2026-10-18)

Runs on main:
- 39136819132, the first push run:
  https://github.com/mikljohansson/fnirsi-1013d-mcp/actions/runs/39136819132
- 39136866307, the second push run:
  https://github.com/mikljohansson/fnirsi-1013d-mcp/actions/runs/39136866307
- 39137812868, a workflow_dispatch run after the action update:
  https://github.com/mikljohansson/fnirsi-1013d-mcp/actions/runs/39137812868

- [x] apt on ubuntu-24.04 installs `libnewlib-arm-none-eabi` 4.4.0.20231231-2 with `gcc-arm-none-eabi`
      13.2.1. `make` links.
- [x] The GCC 10.3.1 `.deb`s download over https and pass their pinned sha256 values. This happened on
      the first run, on a cache miss.
- [x] The second and later runs restore `gcc10-Linux-...` and `atlan-v1.00o5-...` from the cache. The
      reference step prints "nothing to download". The reference comparison (removed in fw 0.2.47) printed
      IDENTICAL on every run.
- [x] The mcp tests pass on windows-latest with Python 3.12 and 3.13. The first run had one flaky
      failure on ubuntu with Python 3.13, in `test_set_acq_state_during_a_pending_wait_aborts_it`. Its
      cause was a one-shot cancel callback. `Transport.deferred` resent the WAIT after the abort, and
      the device answered the resend with OK. The fix latches the cancel. It landed in 4f17414, and
      later runs pass.

Found and fixed on the first day:
- The v4 actions ran on deprecated Node 20. CI now uses Node 24 majors (cd422b9).
- ubuntu-latest moves to Ubuntu 26 from 2026-10-19, so the mcp Linux runner is pinned to ubuntu-24.04
  (cd422b9).
- The concurrency group used to be per ref. With several pushes in a row, a newer pending run replaced
  an older one, so some commits got no CI. Pushes now have one group per commit.

After the first tag, `v0.3.0`:

- [ ] `release.yml` downloads the `firmware-image` artifact that was uploaded inside the called
      `ci.yml`. The artifact exists on every CI run, about 225 KB zipped.
- [ ] `gh release create` succeeds with the job-level `contents: write` while the repository default
      is read.
