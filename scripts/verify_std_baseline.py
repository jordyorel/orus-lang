#!/usr/bin/env python3
"""Ensure the standard library tree remains empty and no dormant std imports remain."""

from __future__ import annotations

import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
STD_DIR = REPO_ROOT / "std"

BANNED_IMPORT_PREFIXES = (
    "use std.",
    "use std/",
)

BANNED_ATTRIBUTE_PATTERNS = (
    "@[core",
    "[core(",
)

def main() -> int:
    errors: list[str] = []

    if STD_DIR.exists():
        for candidate in STD_DIR.rglob("*.orus"):
            rel = candidate.relative_to(REPO_ROOT)
            errors.append(f"unexpected std module present: {rel}")

    for source in REPO_ROOT.rglob("*.orus"):
        try:
            text = source.read_text(encoding="utf-8")
        except UnicodeDecodeError as exc:
            errors.append(f"failed to read {source}: {exc}")
            continue

        for pattern in BANNED_IMPORT_PREFIXES:
            if pattern in text:
                rel = source.relative_to(REPO_ROOT)
                errors.append(f"forbidden import '{pattern}' found in {rel}")

        for pattern in BANNED_ATTRIBUTE_PATTERNS:
            if pattern in text:
                rel = source.relative_to(REPO_ROOT)
                errors.append(f"forbidden attribute pattern '{pattern}' found in {rel}")

    if errors:
        for error in errors:
            print(error)
        return 1

    print("std baseline verified: no modules or forbidden std imports detected")
    return 0


if __name__ == "__main__":
    sys.exit(main())
