#!/usr/bin/env python3
"""Check a release Linux ELF executable using binutils (readelf and nm)."""

import argparse
import os
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", help="path to the release Linux executable")
    args = parser.parse_args()
    env = dict(os.environ, LC_ALL="C")
    try:
        elf = subprocess.check_output(
            ["readelf", "-W", "-h", "-l", "-d", args.binary], text=True, env=env
        )
        symbols = subprocess.check_output(
            ["nm", "-D", args.binary], text=True, env=env
        )
    except (OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"FAIL: unable to inspect {args.binary}: {error}\n")

    stack = re.search(r"^\s*GNU_STACK\s+(.+)$", elf, re.MULTILINE)
    checks = {
        "PIE (ELF DYN with interpreter)": bool(
            re.search(r"Type:\s+DYN\b", elf)
            and re.search(r"^\s*INTERP\s", elf, re.MULTILINE)
        ),
        "RELRO segment": "GNU_RELRO" in elf,
        "immediate binding (full RELRO)": bool(
            re.search(r"\(FLAGS\).*\bBIND_NOW\b|\(FLAGS_1\).*\bNOW\b", elf)
        ),
        "non-executable GNU_STACK": bool(stack and "E" not in stack.group(1)),
        "stack protector symbol": "__stack_chk_fail" in symbols,
        "fortified libc symbols": bool(re.search(r"\b__\w+_chk(?:@|\s)", symbols)),
    }
    for name, passed in checks.items():
        print(f"{'PASS' if passed else 'FAIL'}: {name}")
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
