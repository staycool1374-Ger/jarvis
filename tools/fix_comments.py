#!/usr/bin/env python3
"""Fix long comment lines by wrapping them at ~78 characters."""

import re
import sys
from pathlib import Path

MAX_LINE = 80
WRAP_AT = 78  # Leave room for "// " prefix


def wrap_comment_line(line: str, indent: str) -> list[str]:
    """Wrap a single comment line at WRAP_AT characters."""
    # Remove comment prefix
    if line.startswith("//"):
        prefix = "//"
        content = line[2:]
    elif line.startswith("/*"):
        # Handle block comments - don't wrap these
        return [line]
    else:
        return [line]

    content = content.lstrip()

    if len(line) <= MAX_LINE:
        return [line]

    # Split into words and rebuild lines
    words = content.split()
    if not words:
        return [line]

    result = []
    current = indent + prefix + " "

    for word in words:
        if len(current) + len(word) + 1 > WRAP_AT:
            result.append(current.rstrip())
            current = indent + prefix + " " + word + " "
        else:
            current += word + " "

    if current.strip():
        result.append(current.rstrip())

    return result


def fix_file(path: Path) -> int:
    """Fix long comment lines in a file. Returns number of lines fixed."""
    text = path.read_text(encoding="utf-8", errors="ignore")
    lines = text.split("\n")
    modified = False
    fixed = 0

    for i, line in enumerate(lines):
        if len(line) > MAX_LINE:
            # Check if it's a comment line
            stripped = line.lstrip()
            if stripped.startswith("//") and not stripped.startswith("///"):
                indent = line[:len(line) - len(stripped)]
                wrapped = wrap_comment_line(stripped, indent)
                if len(wrapped) > 1 or wrapped[0] != line:
                    lines[i:i+1] = wrapped
                    modified = True
                    fixed += 1

    if modified:
        path.write_text("\n".join(lines), encoding="utf-8")

    return fixed


def main():
    if len(sys.argv) < 2:
        print("Usage: fix_comments.py <file_or_dir> [--dry-run]")
        sys.exit(1)

    target = Path(sys.argv[1])
    dry_run = "--dry-run" in sys.argv

    if target.is_file():
        files = [target]
    else:
        # Find C/C++ files
        files = list(target.rglob("*.cpp")) + list(target.rglob("*.hpp")) + \
                list(target.rglob("*.c")) + list(target.rglob("*.h"))

    total_fixed = 0
    for f in files:
        # Skip font.hpp and other data tables
        if "font.hpp" in str(f):
            continue
        if dry_run:
            text = f.read_text(encoding="utf-8", errors="ignore")
            for i, line in enumerate(text.split("\n"), 1):
                if len(line) > MAX_LINE and line.lstrip().startswith("//"):
                    print(f"{f}:{i}: would wrap ({len(line)} chars)")
        else:
            fixed = fix_file(f)
            if fixed:
                print(f"{f}: fixed {fixed} comment line(s)")
            total_fixed += fixed

    if not dry_run:
        print(f"Total comment lines fixed: {total_fixed}")


if __name__ == "__main__":
    main()