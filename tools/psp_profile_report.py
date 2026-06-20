#!/usr/bin/env python3
"""Generate a PSP gprof text report from a matching ELF and gmon file."""

import argparse
import hashlib
import re
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Dict, Optional


GPROF_CANDIDATES = (
    "psp-gprof",
    "mipsel-psp-gprof",
    "mips-psp-gprof",
)


def find_gprof():
    for candidate in GPROF_CANDIDATES:
        path = shutil.which(candidate)
        if path:
            return path
    return None


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def parse_sha256sums(path: Path) -> Dict[str, str]:
    sums = {}
    if not path.is_file():
        return sums
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            parts = line.strip().split(None, 1)
            if len(parts) == 2:
                sums[Path(parts[1]).name] = parts[0]
    return sums


def read_metadata(path: Path) -> Dict[str, str]:
    metadata = {}
    if not path.is_file():
        return metadata
    with path.open("r", encoding="utf-8") as handle:
        for line in handle:
            key, sep, value = line.rstrip("\n").partition("=")
            if sep:
                metadata[key] = value
    return metadata


def validate_report_text(report: str) -> Optional[str]:
    if not report.strip():
        return "gprof produced an empty report"
    if "no time accumulated" in report.lower():
        return "gprof reports no samples/time accumulated"

    sample_rows = 0
    unresolved_rows = 0
    in_flat_profile = False
    row_re = re.compile(r"^\s*\d+\.\d+\s+")

    for line in report.splitlines():
        if line.startswith("Flat profile:"):
            in_flat_profile = True
            continue
        if in_flat_profile and line.startswith("Call graph"):
            break
        if in_flat_profile and row_re.match(line):
            sample_rows += 1
            if "??" in line or re.search(r"\b0x[0-9a-fA-F]+\b", line):
                unresolved_rows += 1

    if sample_rows == 0:
        return "gprof flat profile contains no sampled function rows"
    if sample_rows >= 10 and unresolved_rows * 100 >= sample_rows * 50:
        return "gprof output appears mostly unresolved; check that the matching unstripped ELF was used"
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", help="matching unstripped PSP ELF")
    parser.add_argument("gmon", help="raw gmon output copied from the PSP")
    parser.add_argument("out", help="destination report text file")
    parser.add_argument("--build-id", help="expected build_id from profile_build_metadata.txt")
    args = parser.parse_args()

    elf = Path(args.elf)
    gmon = Path(args.gmon)
    out = Path(args.out)

    if not elf.is_file():
        print(f"error: ELF not found: {elf}", file=sys.stderr)
        return 2
    if not gmon.is_file():
        print(f"error: gmon file not found: {gmon}", file=sys.stderr)
        return 2

    gprof = find_gprof()
    if gprof is None:
        names = ", ".join(GPROF_CANDIDATES)
        print(f"error: no PSP-target gprof found in PATH; tried {names}", file=sys.stderr)
        return 2

    elf_sha = sha256(elf)
    gmon_sha = sha256(gmon)
    metadata = read_metadata(elf.parent / "profile_build_metadata.txt")
    if args.build_id and metadata.get("build_id") and metadata["build_id"] != args.build_id:
        print(
            f"error: build id mismatch: expected {args.build_id}, ELF metadata has {metadata['build_id']}",
            file=sys.stderr,
        )
        return 2

    known_sums = parse_sha256sums(elf.parent / "SHA256SUMS")
    expected_elf_sha = known_sums.get(elf.name)
    if expected_elf_sha and expected_elf_sha != elf_sha:
        print(
            f"error: ELF checksum mismatch for {elf.name}: metadata has {expected_elf_sha}, file is {elf_sha}",
            file=sys.stderr,
        )
        return 2

    version_result = subprocess.run([gprof, "--version"], text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    gprof_version = version_result.stdout.splitlines()[0] if version_result.stdout.splitlines() else "unknown"

    out.parent.mkdir(parents=True, exist_ok=True)
    cmd = [gprof, "-b", str(elf), str(gmon)]
    result = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        print(f"error: {' '.join(cmd)} failed with exit code {result.returncode}", file=sys.stderr)
        if result.stderr:
            print(result.stderr, file=sys.stderr, end="")
        return result.returncode

    validation_error = validate_report_text(result.stdout)
    if validation_error is not None:
        print(f"error: {validation_error}", file=sys.stderr)
        return 3

    with out.open("w", encoding="utf-8") as handle:
        handle.write("PSP gprof report\n")
        handle.write(f"gprof: {gprof}\n")
        handle.write(f"gprof version: {gprof_version}\n")
        handle.write(f"ELF: {elf}\n")
        handle.write(f"ELF sha256: {elf_sha}\n")
        handle.write(f"GMON: {gmon}\n\n")
        handle.write(f"GMON sha256: {gmon_sha}\n")
        if metadata.get("build_id"):
            handle.write(f"build id: {metadata['build_id']}\n")
        handle.write("\n")
        handle.write(result.stdout)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
