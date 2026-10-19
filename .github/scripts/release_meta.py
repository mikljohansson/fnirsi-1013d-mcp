#!/usr/bin/env python3
"""Release checks for a tag vX.Y.Z or vX.Y.Z-rcN (used by .github/workflows/release.yml, runnable locally).

- the firmware version (PC_FW_VERSION in firmware/scope/pc_cmds.h, default FIFO map) must equal X.Y.Z
- the mcp package version (mcp/pyproject.toml) must equal X.Y.Z
- CHANGELOG.md must have a non-empty "## [X.Y.Z] - YYYY-MM-DD" section (the developer history)
- docs/release-notes/X.Y.Z.md must exist, be non-empty and have no {{placeholders}}; it becomes the release
  notes (user-facing text, written separately from the CHANGELOG)

A -rcN tag is a dry run of the same commit: the same checks against X.Y.Z, always published as a pre-release.

  python3 .github/scripts/release_meta.py --tag v0.3.0 --notes notes.md
  python3 .github/scripts/release_meta.py --tag v0.3.0-rc1 --notes notes.md
  python3 .github/scripts/release_meta.py --check-changelog          # CI: CHANGELOG structure only

Prints key=value lines (version, prerelease) for $GITHUB_OUTPUT. Exit 1 with ::error:: lines on failure.
"""

import argparse
import re
import sys
import tomllib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TAG_RX = re.compile(r"^v(\d+)\.(\d+)\.(\d+)(-rc\d+)?$")
FW_RX = re.compile(r'^\s*#define\s+PC_FW_VERSION\s+"(\d+\.\d+\.\d+)"', re.M)   # the -m1 variant is skipped
SECTION_RX = re.compile(r"^## \[(?P<ver>[^\]]+)\](?: - (?P<date>\d{4}-\d{2}-\d{2}))?\s*$", re.M)


def firmware_version(root=ROOT):
    found = sorted(set(FW_RX.findall((root / "firmware/scope/pc_cmds.h").read_text(encoding="utf-8"))))
    if len(found) != 1:
        raise ValueError("expected one plain PC_FW_VERSION in firmware/scope/pc_cmds.h, found %r" % found)
    return found[0]


def mcp_version(root=ROOT):
    with open(root / "mcp/pyproject.toml", "rb") as f:
        return tomllib.load(f)["project"]["version"]


def changelog_sections(root=ROOT):
    """[(version, date or None, body)] in file order."""
    text = (root / "CHANGELOG.md").read_text(encoding="utf-8")
    heads = list(SECTION_RX.finditer(text))
    out = []
    for i, m in enumerate(heads):
        end = heads[i + 1].start() if i + 1 < len(heads) else len(text)
        body = text[m.end():end]
        body = re.split(r"^\[[^\]]+\]: ", body, maxsplit=1, flags=re.M)[0]     # drop link references
        out.append((m.group("ver"), m.group("date"), body.strip()))
    return out


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--tag", help="tag name, e.g. v0.3.0")
    ap.add_argument("--notes", type=Path, help="write the release notes (docs/release-notes/X.Y.Z.md) here")
    ap.add_argument("--check-changelog", action="store_true", help="only check CHANGELOG.md structure")
    args = ap.parse_args(argv)
    errors = []

    sections = changelog_sections()
    if not sections or sections[0][0] != "Unreleased":
        errors.append("CHANGELOG.md: the first section must be '## [Unreleased]'")
    for ver, date, _ in sections[1:]:
        if not re.fullmatch(r"\d+\.\d+\.\d+", ver) or not date:
            errors.append("CHANGELOG.md: section '%s' needs the form '## [X.Y.Z] - YYYY-MM-DD'" % ver)

    if not args.check_changelog:
        if not args.tag:
            ap.error("--tag is required unless --check-changelog")
        m = TAG_RX.match(args.tag)
        if not m:
            errors.append("tag %r is not vMAJOR.MINOR.PATCH or vMAJOR.MINOR.PATCH-rcN" % args.tag)
        else:
            version = "%s.%s.%s" % m.group(1, 2, 3)
            fw, py = firmware_version(), mcp_version()
            if fw != version:
                errors.append("firmware/scope/pc_cmds.h PC_FW_VERSION is %s, tag is %s" % (fw, args.tag))
            if py != version:
                errors.append("mcp/pyproject.toml version is %s, tag is %s" % (py, args.tag))
            body = next((b for v, _, b in sections if v == version), None)
            if body is None:
                errors.append("CHANGELOG.md has no '## [%s] - YYYY-MM-DD' section" % version)
            elif not body:
                errors.append("CHANGELOG.md section %s is empty" % version)
            notes_name = "docs/release-notes/%s.md" % version
            notes_path = ROOT / notes_name
            notes = notes_path.read_text(encoding="utf-8").strip() if notes_path.is_file() else ""
            if not notes:
                errors.append("%s is missing or empty" % notes_name)
            elif "{{" in notes:
                errors.append("%s still has {{placeholders}}" % notes_name)
            elif args.notes:
                args.notes.write_text(notes + "\n", encoding="utf-8", newline="\n")
            print("version=%s" % version)
            print("prerelease=%s" % ("true" if m.group(1) == "0" or m.group(4) else "false"))

    for e in errors:
        print("::error::%s" % e, file=sys.stderr)
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
