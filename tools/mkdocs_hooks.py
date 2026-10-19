"""MkDocs hook: links from docs/ to files that are not in the site point at GitHub.

docs/ is the single source and its pages link to ../firmware/README.md, ../mcp/README.md, plans/... as relative
paths that work in a checkout. The site holds only the pages in the nav (docs/plans and docs/mockups are excluded
in mkdocs.yml), so ``mkdocs build --strict`` would call those links broken. This hook rewrites a relative link whose
target exists in the repository but not in the site to the matching GitHub blob/tree URL; a link that is missing
everywhere is left alone and fails the strict build.

Screenshots: a page writes ``![caption](screenshot:NAME)`` alone on a line. When docs/screenshots/NAME.png exists
(tools/screenshots.py writes it) the line becomes the image; until then it becomes a "Screenshot pending" note, so
a screenshot that is not captured yet never breaks the build.
"""

from __future__ import annotations

import os
import re
from pathlib import Path

REPO = "https://github.com/mikljohansson/fnirsi-1013d-mcp"
BRANCH = "main"
ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "docs"
#: directories of docs/ that mkdocs.yml excludes from the site (keep in step with exclude_docs)
EXCLUDED = (DOCS / "plans", DOCS / "mockups")

#: [text](target#anchor "title"); images included. Targets with a scheme or a leading slash are skipped.
LINK = re.compile(r"(\]\()([^)\s#][^)\s#]*)(#[^)\s]*)?((?:\s+\"[^\"]*\")?\))")


def _in(path: Path, parent: Path) -> bool:
    return parent == path or parent in path.parents


def rewrite(page_src: Path, target: str, anchor: str | None) -> str | None:
    """GitHub URL for a relative link to a repository file outside the site, else None (leave the link alone)."""
    if re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", target) or target.startswith("/"):
        return None
    resolved = (page_src.parent / target).resolve()
    if not resolved.exists() or not _in(resolved, ROOT):
        return None
    if _in(resolved, DOCS) and not any(_in(resolved, d) for d in EXCLUDED):
        return None
    kind = "tree" if resolved.is_dir() else "blob"
    return f"{REPO}/{kind}/{BRANCH}/{resolved.relative_to(ROOT).as_posix()}{anchor or ''}"


SCREENSHOT = re.compile(r"^!\[([^\]]*)\]\(screenshot:([A-Za-z0-9_-]+)\)[ \t]*$", re.MULTILINE)


def screenshot(page_src: Path, caption: str, name: str) -> str:
    png = DOCS / "screenshots" / f"{name}.png"
    if png.exists():
        rel = os.path.relpath(png, page_src.parent).replace(os.sep, "/")
        return f"![{caption}]({rel}){{ loading=lazy }}"
    return (f'!!! note "Screenshot pending: {name}"\n\n'
            f"    {caption}. Captured on the unit with `python tools/screenshots.py --only {name}`.")


def on_page_markdown(markdown: str, page, config, files) -> str:
    src = Path(page.file.abs_src_path)
    markdown = SCREENSHOT.sub(lambda m: screenshot(src, m.group(1), m.group(2)), markdown)

    def sub(m: re.Match) -> str:
        new = rewrite(src, m.group(2), m.group(3))
        return m.group(0) if new is None else f"{m.group(1)}{new}{m.group(4)}"

    return LINK.sub(sub, markdown)
