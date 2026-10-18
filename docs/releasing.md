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
| mcp | ubuntu-latest and windows-latest, Python 3.12 and 3.13 | `pip install -e mcp[dev]`, then ruff with the selection from `mcp/AGENTS.md`, then pytest |
| docs-lint | ubuntu-24.04 | `docs/protocol-vectors.json` against the Python checker and the C checker; `CHANGELOG.md` structure |

The firmware job runs these steps in order:

1. Install the apt toolchain (GCC 13.2.1).
2. Unpack Atlan4's GCC 10.3.1 with `firmware/tools/fetch_gcc10.sh`. It is cached and keyed on the
   script's hash.
3. Fetch Atlan4's v1.00o5 reference files with `tools/make_release_sd_package.py reference`. They are
   cached and checked by sha256 on every run.
4. Run `make check-reference`. Its output must contain `IDENTICAL`.
5. Run `make gcc10`, then `make`, then `make PC_DEBUG=1 BUILD=build-dbg`.
6. Run `make -C host_tests test`, which includes `check_vectors.py`.

The job uploads `build-gcc10-pc/fnirsi_1013d.bin` and its `.sha256` as the `firmware-image` artifact.
The step summary lists the size and sha256 of each of the four builds.

Warnings are handled exactly as the repository already handles them. The firmware builds keep the
upstream warnings and do not use `-Werror`. The host tests build with `-Werror`, as their Makefile
says. The warning-list comparison in `firmware/AGENTS.md` stays a manual review step.

The release image is reproducible. `make gcc10` runs with `SOURCE_DATE_EPOCH` set to the commit time,
so `build_time.h` is fixed. The image also embeds the 8-digit commit hash (`PC_FW_BUILD`). Two clean
clones of the same commit gave the same sha256. To rebuild a release image locally:

```
git checkout vX.Y.Z
cd firmware/scope
SOURCE_DATE_EPOCH=$(git log -1 --format=%ct) make gcc10
sha256sum build-gcc10-pc/fnirsi_1013d.bin      # equals the release's SHA256SUMS line
```

A bench image built without `SOURCE_DATE_EPOCH`, or at another commit, has a different hash. The hashes
in `docs/hardware-status.md` come from such local builds.

## 2. Cutting a release

The firmware and the mcp package share one version. The first tag is planned as v0.3.0
(`CHANGELOG.md`, `docs/plans/roadmap.md` section 1).

1. Bump the version in two places:
   - `firmware/scope/pc_cmds.h`: both `PC_FW_VERSION` lines, `"X.Y.Z"` and `"X.Y.Z-m1"`.
   - `mcp/pyproject.toml`: `version = "X.Y.Z"`.
2. In `CHANGELOG.md`, move the entries from `## [Unreleased]` into a new section
   `## [X.Y.Z] - YYYY-MM-DD` below it. Leave `## [Unreleased]` in place and empty. Use the
   Added / Changed / Fixed / Removed headings. The section text, without the heading, becomes the
   release notes.
3. Before committing, run the same checks as CI:

   ```
   wsl -d Ubuntu -- bash -lc "cd /mnt/d/src/fnirsi-1013d-ai/firmware/scope && make check-reference && make gcc10 && make && make -C host_tests test"
   cd mcp && .venv/Scripts/python -m pytest -q
   ```

   Then check the tag metadata. Exit 0 means the tag will be accepted.

   ```
   python .github/scripts/release_meta.py --tag vX.Y.Z --notes notes.md
   ```

4. Commit on main with the subject `release: vX.Y.Z` and push. Wait for CI to pass.
5. Tag that commit and push the tag:

   ```
   git tag -a vX.Y.Z -m "fnirsi-1013d-mcp X.Y.Z"
   git push origin vX.Y.Z
   ```

### What release.yml does

1. **check**: `release_meta.py` fails the run if any of these hold:
   - the tag is not `vMAJOR.MINOR.PATCH`;
   - `PC_FW_VERSION` or the mcp version differs from the tag;
   - the CHANGELOG section is missing or empty.
2. **ci**: the whole of `ci.yml` runs as a reusable workflow on the tag commit.
3. **publish** first checks that the image is the one CI built for this tag. The artifact's sha256 must
   still match, and the `#VER` literal in the image must report the tag's version. The job then attaches
   these files to a GitHub release:

| file | content |
|---|---|
| `fnirsi_1013d-fw-X.Y.Z.bin` | the scope image (eGON.EXE, GCC 10 protocol build) for FEL or SD sector 750 |
| `fnirsi_1013d-sdtools-X.Y.Z.zip` | `make_release_sd_package.py`, `make_sd_image.py`, `mksunxi.py` |
| `fnirsi1013d-X.Y.Z-py3-none-any.whl`, `fnirsi1013d-X.Y.Z.tar.gz` | the mcp package (`python -m build mcp`) |
| `SHA256SUMS` | sha256 of every file above, also printed in the release notes |

Before it publishes, the job runs the SD tools from the zip on a clean runner. They download Atlan4's
package and build an SD package with the new image, which tests the path users take.

Tags with major version 0 are marked as pre-release. The release is created with
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
     8 digits of the tag commit (`git rev-parse --short=8 vX.Y.Z`).
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
published, from Atlan4. CI fetches the same zip for `make check-reference`, and that stays inside the CI
cache. `firmware/reference/` stays gitignored.

If Atlan4 publishes the bootloader v0.8 or updater source later:

1. Import it under `firmware/bootloader/`, with its licence and the upstream commit noted. Build it with
   a plain Makefile, like `firmware/scope`.
2. Prove a byte-identical rebuild of the binaries in the v1.00o5 package. Add a `make check-reference`
   style target and run it in CI's firmware job.
3. Only then attach the bootloader, the updater or a full SD package to releases. Add a
   `fnirsi_1013d-fw-X.Y.Z-sdpackage.bin` step to `release.yml` from `make package`. The SD tools zip can
   then become optional.
4. Update this section, `firmware/README.md` section 3 and `CHANGELOG.md`.

If the pinned download disappears, for example after a history rewrite in Atlan4's repository, the
release job's SD tools test fails. CI also fails on a cache miss. In that case, find the zip, verify the
two inner sha256 values in `tools/make_release_sd_package.py`, and update `ATLAN_COMMIT`. The inner
files must not change. If Atlan4 publishes a new package, that is a new reference and needs a deliberate
change.

## 6. Things to confirm after the first real run on GitHub

The workflows were validated locally with PyYAML and actionlint 1.7.7 (with shellcheck). The composite
action's steps were also run in a fresh clone in WSL Ubuntu 24.04. They have not yet run on GitHub.
After the first push and the first tag, confirm:

- apt on ubuntu-24.04 installs `libnewlib-arm-none-eabi` together with `gcc-arm-none-eabi`.
- The jammy `.deb` URLs in `fetch_gcc10.sh`, on http://archive.ubuntu.com, are reachable from runners.
- The second run reports cache hits for GCC 10 and for the reference files.
- The mcp tests pass on windows-latest with Python 3.13. The local runs used Windows with 3.12 and
  WSL with 3.12.
- On the tag run, `release.yml` can download the `firmware-image` artifact uploaded inside the called
  `ci.yml`.
- `gh release create` succeeds with the default `GITHUB_TOKEN`. The repository setting "Workflow
  permissions" must allow `contents: write` for the publish job.
