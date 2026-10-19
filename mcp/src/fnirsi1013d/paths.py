"""Where the package keeps its own files, and where the repository checkout is when it runs from one.

Installed from a wheel, nothing may land next to the package (inside ``site-packages``) and nothing may be read from
``../docs`` or ``../backups``: the per-user home holds the server's output, and every input file is an argument.
From a source checkout (``pip install -e mcp``) the repository's files are used as defaults, but only when they exist.

Per-user home: ``FNIRSI_MCP_HOME`` if set, else ``~/.fnirsi1013d``. The server keeps ``data/`` (``.npz`` files, per-unit
calibration, Bode thru calibrations) and ``logs/audit.jsonl`` there unless ``FNIRSI_MCP_DATA_DIR`` /
``FNIRSI_MCP_AUDIT_LOG`` or the matching arguments say otherwise.
"""

from __future__ import annotations

import os
from pathlib import Path

HOME_ENV = "FNIRSI_MCP_HOME"


def user_home() -> Path:
    """The per-user directory for the server's data and logs (not created here)."""
    v = os.environ.get(HOME_ENV)
    return Path(v).expanduser() if v else Path.home() / ".fnirsi1013d"


def checkout() -> Path | None:
    """The repository root when this package is imported from a source checkout (``mcp/src/fnirsi1013d``), else None."""
    here = Path(__file__).resolve()
    if len(here.parents) < 4:
        return None
    root = here.parents[3]
    if (root / "mcp" / "pyproject.toml").is_file() and (root / "docs" / "protocol.md").is_file():
        return root
    return None


def checkout_path(*parts: str) -> Path | None:
    """``<checkout>/<parts>`` when running from a checkout and that file or directory exists, else None."""
    root = checkout()
    if root is None:
        return None
    p = root.joinpath(*parts)
    return p if p.exists() else None


def checkout_dir(*parts: str) -> Path | None:
    """``<checkout>/<parts>`` when running from a checkout (the directory need not exist yet), else None."""
    root = checkout()
    return None if root is None else root.joinpath(*parts)
