#!/usr/bin/env python3
"""Generate a PSP gprof text report from a matching ELF and gmon file."""

import argparse
import shutil
import subprocess
import sys
from pathlib import Path


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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", help="matching unstripped PSP ELF")
    parser.add_argument("gmon", help="raw gmon output copied from the PSP")
    parser.add_argument("out", help="destination report text file")
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

    out.parent.mkdir(parents=True, exist_ok=True)
    cmd = [gprof, "-b", str(elf), str(gmon)]
    result = subprocess.run(cmd, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode != 0:
        print(f"error: {' '.join(cmd)} failed with exit code {result.returncode}", file=sys.stderr)
        if result.stderr:
            print(result.stderr, file=sys.stderr, end="")
        return result.returncode

    with out.open("w", encoding="utf-8") as handle:
        handle.write("PSP gprof report\n")
        handle.write(f"gprof: {gprof}\n")
        handle.write(f"ELF: {elf}\n")
        handle.write(f"GMON: {gmon}\n\n")
        handle.write(result.stdout)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
