#!/usr/bin/env python3
"""
Auto-fix lines exceeding 100 characters by breaking at appropriate positions.
"""

import re
import sys
from pathlib import Path


def fix_line(line: str, max_len: int = 100) -> list[str]:
    """Break a long line at appropriate positions."""
    if len(line) <= max_len:
        return [line]

    # Try to break at various positions
    # 1. After comma in function calls/arguments
    # 2. After operators (+, -, *, /, ==, !=, &&, ||, etc.)
    # 3. After opening brace/bracket
    # 4. Before closing brace/bracket

    # Find good break points
    break_patterns = [
        (r'(,\s*)', r'\1\n'),           # After comma
        (r'(\s*\+\s*)', r'\1\n'),       # After +
        (r'(\s*-\s*)', r'\1\n'),        # After -
        (r'(\s*\*\s*)', r'\1\n'),       # After *
        (r'(\s*/\s*)', r'\1\n'),        # After /
        (r'(\s*==\s*)', r'\1\n'),       # After ==
        (r'(\s*!=\s*)', r'\1\n'),       # After !=
        (r'(\s*&&\s*)', r'\1\n'),       # After &&
        (r'(\s*\|\|\s*)', r'\1\n'),     # After ||
        (r'(\s*\+\=\s*)', r'\1\n'),     # After +=
        (r'(\s*\-\=\s*)', r'\1\n'),     # After -=
        (r'(\s*\*\=\s*)', r'\1\n'),     # After *=
        (r'(\s*/\=\s*)', r'\1\n'),      # After /=
        (r'(\s*\<\<\s*)', r'\1\n'),     # After <<
        (r'(\s*\>\>\s*)', r'\1\n'),     # After >>
        (r'(\s*\|\s*)', r'\1\n'),       # After |
        (r'(\s*\&\s*)', r'\1\n'),       # After &
        (r'(\s*\^\s*)', r'\1\n'),       # After ^
        (r'(\s*\?\s*)', r'\1\n'),       # After ?
        (r'(\s*\:\s*)', r'\1\n'),       # After :
        (r'(\(\s*)', r'\1\n'),          # After (
        (r'(\s*\))', r'\n\1'),          # Before )
        (r'(\{\s*)', r'\1\n'),          # After {
        (r'(\s*\})', r'\n\1'),          # Before }
        (r'(\[\s*)', r'\1\n'),          # After [
        (r'(\s*\])', r'\n\1'),          # Before ]
    ]

    # For C++ code, we need to be smarter - don't break inside strings, comments
    # Simple approach: try to break at commas in function calls first

    # Check if it's a comment line
    if line.strip().startswith('//'):
        return [line]

    # Check if it's a preprocessor directive
    if line.strip().startswith('#'):
        return [line]

    # Try breaking at commas first (most common in C++)
    if ',' in line and not ('"' in line and line.count('"') % 2 == 0):
        parts = line.split(',')
        result = []
        current = ""
        for i, part in enumerate(parts):
            if i < len(parts) - 1:
                part = part + ','
            test = current + part
            if len(test) > max_len and current:
                result.append(current.rstrip())
                current = "    " + part.lstrip()
            else:
                current = test
        if current:
            result.append(current.rstrip())
        if len(result) > 1:
            return result

    # Try breaking at operators
    for pattern, replacement in break_patterns:
        if re.search(pattern, line):
            # Only break if not in string literal
            new_line = re.sub(pattern, replacement, line, count=1)
            if len(new_line) <= max_len or '\n' in new_line:
                return new_line.split('\n')

    # Fallback: hard break at max_len
    return [line[:max_len], "    " + line[max_len:]]


def process_file(filepath: Path) -> bool:
    """Process a single file, fixing long lines."""
    with open(filepath, 'r') as f:
        lines = f.readlines()

    modified = False
    new_lines = []

    for line in lines:
        stripped = line.rstrip('\n')
        if len(stripped) > 100:
            fixed = fix_line(stripped)
            if len(fixed) > 1 or fixed[0] != stripped:
                new_lines.extend(f + '\n' for f in fixed)
                modified = True
            else:
                new_lines.append(line)
        else:
            new_lines.append(line)

    if modified:
        with open(filepath, 'w') as f:
            f.writelines(new_lines)
        print(f"Fixed: {filepath}")
    return modified


def main():
    if len(sys.argv) < 2:
        print("Usage: fix_long_lines.py <file1> [file2] ...")
        sys.exit(1)

    for arg in sys.argv[1:]:
        filepath = Path(arg)
        if filepath.exists():
            process_file(filepath)
        else:
            print(f"File not found: {filepath}")


if __name__ == '__main__':
    main()