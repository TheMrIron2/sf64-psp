#!/usr/bin/env python3
"""Fail the PSP build when generated asset enum comments corrupt C tokens."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADER_PATHS = (ROOT / "include/sf64object.h", ROOT / "include/sf64event.h")
ASSET_ROOT = ROOT / "src/assets"
TOKEN_RE = re.compile(r"\b(?:OBJ|EVID|EVACT)_[A-Za-z0-9_]+(?:-[A-Za-z][A-Za-z0-9]*)?\.?")
VALID_RE = re.compile(r"\b(?:OBJ|EVID|EVACT)_[A-Z0-9_]+\b")


def load_valid_tokens() -> set[str]:
    valid: set[str] = set()
    for path in HEADER_PATHS:
        valid.update(VALID_RE.findall(path.read_text(errors="ignore")))
    return valid


def describe_bad_token(token: str, valid_tokens: set[str]) -> str | None:
    clean = token.rstrip(".")
    if clean in valid_tokens:
        return None

    if "-" in clean:
        prefix = clean.split("-", 1)[0]
        if prefix in valid_tokens:
            return f"{token} looks like '{prefix}' joined to a comment"

    for end in range(len(clean) - 1, 0, -1):
        prefix = clean[:end]
        if prefix in valid_tokens:
            suffix = clean[end:]
            if suffix and (suffix[0].isalpha() or suffix[0].isdigit()):
                return f"{token} looks like '{prefix}' joined to '{suffix}'"

    return f"{token} is not a known generated asset enum"


def main() -> int:
    valid_tokens = load_valid_tokens()
    failures: list[str] = []

    for path in sorted(ASSET_ROOT.rglob("*.c")):
        for line_no, line in enumerate(path.read_text(errors="ignore").splitlines(), 1):
            for match in TOKEN_RE.finditer(line):
                reason = describe_bad_token(match.group(0), valid_tokens)
                if reason is not None:
                    rel = path.relative_to(ROOT)
                    failures.append(f"{rel}:{line_no}: {reason}")

    if failures:
        print("PSP asset preflight failed: generated enum/comment corruption found.", file=sys.stderr)
        for failure in failures[:80]:
            print(failure, file=sys.stderr)
        if len(failures) > 80:
            print(f"... and {len(failures) - 80} more", file=sys.stderr)
        return 1

    print("PSP asset preflight OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
