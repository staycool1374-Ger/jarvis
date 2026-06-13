#!/usr/bin/env python3
"""
Generate constructor initializer list from class member declarations.

Usage: python3 gen_ctor_init.py <header_file> [ClassName]
"""

import re
import sys
from pathlib import Path


def extract_members(content: str, class_name: str = None) -> list:
    """Extract member variables from a class definition."""
    members = []

    # Find class/struct definition
    if class_name:
        class_pattern = rf'(?:class|struct)\s+{re.escape(class_name)}\s*{{'
    else:
        class_pattern = r'(?:class|struct)\s+(\w+)\s*\{'

    class_match = re.search(class_pattern, content)
    if not class_match:
        return members

    if not class_name:
        class_name = class_match.group(1)

    # Find the class body (between { and matching })
    start = class_match.end()
    brace_count = 1
    end = start
    while end < len(content) and brace_count > 0:
        if content[end] == '{':
            brace_count += 1
        elif content[end] == '}':
            brace_count -= 1
        end += 1

    class_body = content[start:end-1]

    # Extract member variables (skip methods, static members, etc.)
    # Pattern: type name; or type name = value;
    member_pattern = re.compile(
        r'^\s*(?:static\s+)?(?:const\s+)?'
        r'(?:mutable\s+)?'
        r'([A-Za-z_][A-Za-z0-9_:<>]*\s*[*&]?)\s+'
        r'([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*[^;]+)?\s*;',
        re.MULTILINE
    )

    for match in member_pattern.finditer(class_body):
        full_type = match.group(1).strip()
        name = match.group(2).strip()

        # Skip static members, function declarations, etc.
        if full_type.startswith('static'):
            continue
        if '(' in full_type or ')' in full_type:
            continue

        # Clean up type
        type_clean = full_type.replace('*', '').replace('&', '').strip()
        if type_clean.endswith('const'):
            type_clean = type_clean[:-5].strip()

        # Skip if it looks like a method
        if name in ['init', 'lock', 'unlock', 'send', 'receive', 'find', 'get']:
            continue

        members.append((type_clean, name, full_type))

    return members, class_name


def generate_init_list(members: list, indent: int = 4) -> str:
    """Generate constructor initializer list."""
    if not members:
        return ""

    lines = []
    for i, (type_name, var_name, full_type) in enumerate(members):
        # Default initialization based on type
        if '*' in full_type:
            init_val = "nullptr"
        elif 'bool' in type_name:
            init_val = "false"
        elif any(t in type_name for t in ['int', 'uint', 'size_t', 'uint64_t', 'uint32_t', 'uint16_t', 'uint8_t']):
            init_val = "0"
        elif 'float' in type_name or 'double' in type_name:
            init_val = "0.0"
        elif 'char' in type_name and '*' not in full_type:
            init_val = "'\\0'"
        else:
            init_val = "{}"

        prefix = " " * indent
        if i == 0:
            lines.append(f"{prefix}: {var_name}({init_val})")
        else:
            lines.append(f"{prefix}, {var_name}({init_val})")

    return "\n".join(lines)


def main():
    if len(sys.argv) < 2:
        print("Usage: python3 gen_ctor_init.py <header_file> [ClassName]")
        sys.exit(1)

    file_path = Path(sys.argv[1])
    class_name = sys.argv[2] if len(sys.argv) > 2 else None

    if not file_path.exists():
        print(f"Error: File not found: {file_path}")
        sys.exit(1)

    content = file_path.read_text()
    members, found_class = extract_members(content, class_name)

    if not members:
        print(f"No members found for class {class_name or 'any'}")
        sys.exit(1)

    print(f"// Constructor initializer list for {found_class}")
    print(f"{found_class}()")
    print(generate_init_list(members))
    print("    {}")


if __name__ == "__main__":
    main()