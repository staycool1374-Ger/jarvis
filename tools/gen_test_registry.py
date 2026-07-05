#!/usr/bin/env python3
"""Generate a static test registry header from C++ test files.

Scans a directory tree for .cpp files, extracts JARVIS_TEST() macro
invocations, and produces test_registry.gen.hpp with a static inline
array of TestMeta entries.

Pattern A (with metadata):
    JARVIS_TEST(name, "PRE: conds | POST: conds") { ... }

Pattern B (legacy, no metadata):
    JARVIS_TEST(name) { ... }
"""

import argparse
import os
import re
import sys

RE_TEST = re.compile(
    r'JARVIS_TEST\s*\(\s*'
    r'(\w+)\s*'
    r'(?:,\s*"([^"]*)"\s*)?'
    r'\)',
    re.DOTALL,
)

RE_META = re.compile(
    r'PRE:\s*(.*?)\s*\|\s*POST:\s*(.*)',
    re.DOTALL,
)

DEFAULT_PRE = "none"
DEFAULT_POST = "none"


def parse_metadata(raw: str | None):
    if raw is None:
        return DEFAULT_PRE, DEFAULT_POST
    m = RE_META.search(raw)
    if m:
        return m.group(1).strip(), m.group(2).strip()
    return raw.strip(), DEFAULT_POST


def split_daemons(raw: str):
    if not raw or raw.strip().lower() == "none":
        return []
    return [d.strip() for d in raw.split(",") if d.strip()]


def strip_comments(text: str) -> str:
    """Remove C++ single-line comments (// ...) from the text.
    Multi-line comments (/* ... */) are preserved; JARVIS_TEST is
    never inside them in practice."""
    result: list[str] = []
    in_string = False
    in_char = False
    for line in text.splitlines(keepends=True):
        i = 0
        n = len(line)
        out = []
        while i < n:
            c = line[i]
            if c == '"' and not in_char:
                in_string = not in_string
            elif c == "'" and not in_string:
                in_char = not in_char
            if not in_string and not in_char and i + 1 < n:
                if c == '/' and line[i + 1] == '/':
                    # Skip the rest of the line but preserve newline
                    if line.endswith("\r\n"):
                        out.append("\r\n")
                    elif line.endswith("\n"):
                        out.append("\n")
                    break
            out.append(c)
            i += 1
        result.append("".join(out))
    return "".join(result)


def strip_disabled_blocks(text: str) -> str:
    """Remove #if 0 ... #endif blocks (including nested) from C++ source."""
    result: list[str] = []
    disabled_depth = 0
    for line in text.splitlines(keepends=True):
        stripped = line.strip()
        if stripped.startswith("#if "):
            raw_cond = stripped[4:].strip()
            # Strip C++-style comments from the condition
            comment_pos = raw_cond.find("//")
            if comment_pos >= 0:
                raw_cond = raw_cond[:comment_pos].strip()
            if raw_cond == "0":
                disabled_depth += 1
                continue
        elif stripped.startswith("#ifdef ") or stripped.startswith("#ifndef "):
            pass
        elif stripped.startswith("#elif"):
            if stripped.lstrip("#elif").strip() == "0":
                # entering else branch of #if 0 ... #elif 0
                if disabled_depth > 0:
                    disabled_depth += 1
                    continue
        elif stripped.startswith("#else"):
            if disabled_depth > 0:
                disabled_depth += 1
                continue
        elif stripped.startswith("#endif"):
            if disabled_depth > 0:
                disabled_depth -= 1
                continue
        if disabled_depth > 0:
            continue
        result.append(line)
    return "".join(result)


def scan_tests(root_dir: str, file_list: list[str] | None = None):
    tests = []
    sources: list[str] = []
    if file_list:
        for f in file_list:
            f = f.strip()
            if f and f.endswith(".cpp"):
                sources.append(f)
    else:
        for dirpath, _, filenames in os.walk(root_dir):
            for fn in filenames:
                if fn.endswith(".cpp"):
                    sources.append(os.path.join(dirpath, fn))

    for path in sources:
        if not os.path.exists(path):
            print(f"warning: {path} not found, skipping", file=sys.stderr)
            continue
        try:
            with open(path, "r", encoding="utf-8", errors="replace") as f:
                content = f.read()
        except OSError as e:
            print(f"error: cannot read {path}: {e}", file=sys.stderr)
            continue
        content = strip_comments(content)
        content = strip_disabled_blocks(content)
        for m in RE_TEST.finditer(content):
            name = m.group(1)
            meta_raw = m.group(2)
            pre, post = parse_metadata(meta_raw)
            tests.append((name, pre, post))
    return tests


def gen_setup(name: str, daemons: list[str]):
    if not daemons:
        return f"inline void setup_{name}() {{}}"
    calls = "\n        ".join(
        f'kernel::daemon::ensure_running("{d}");' for d in daemons
    )
    return f"inline void setup_{name}() {{\n        {calls}\n    }}"


def gen_teardown(name: str, daemons: list[str]):
    if not daemons:
        return f"inline void teardown_{name}() {{}}"
    calls = "\n        ".join(
        f'kernel::daemon::terminate("{d}");' for d in daemons
    )
    return f"inline void teardown_{name}() {{\n        {calls}\n    }}"


def generate(tests, output_path: str):
    lines = ["#pragma once"]

    for name, _, _ in tests:
        lines.append(f"void test_{name}();")
    lines.append("")

    for name, pre_raw, post_raw in tests:
        pre_daemons = split_daemons(pre_raw)
        post_daemons = split_daemons(post_raw)
        lines.append(gen_setup(name, pre_daemons))
        lines.append(gen_teardown(name, post_daemons))
    lines.append("")

    lines.append(
        "struct TestMeta {"
        " const char* name;"
        " const char* pre_conditions;"
        " const char* post_conditions;"
        " void (*test_func)();"
        " void (*setup_func)();"
        " void (*teardown_func)();"
        " };"
    )
    lines.append("")

    lines.append("inline const TestMeta generated_tests[] = {")
    for name, pre, post in tests:
        lines.append(
            f'  {{"{name}", "{pre}", "{post}",'
            f' &test_{name}, &setup_{name}, &teardown_{name}}},'
        )
    lines.append("};")
    lines.append("")

    lines.append(
        "inline constexpr size_t generated_tests_count = "
        "sizeof(generated_tests) / sizeof(TestMeta);"
    )
    lines.append("")

    content = "\n".join(lines) + "\n"
    try:
        with open(output_path, "w", encoding="utf-8") as f:
            f.write(content)
    except OSError as e:
        print(f"error: cannot write {output_path}: {e}", file=sys.stderr)
        sys.exit(1)
    print(f"generated {output_path}: {len(tests)} tests")


def main():
    ap = argparse.ArgumentParser(
        description="Generate a static test registry header from C++ test files."
    )
    ap.add_argument(
        "input_dir",
        help="Root directory to scan for .cpp files",
    )
    ap.add_argument(
        "output_file",
        help="Path for the generated header (e.g. test_registry.gen.hpp)",
    )
    ap.add_argument(
        "--file-list",
        help="Path to a file listing source files to scan (one per line). "
             "When set, only these files are scanned instead of walking input_dir.",
    )
    args = ap.parse_args()

    if not os.path.isdir(args.input_dir):
        print(f"error: input directory '{args.input_dir}' not found", file=sys.stderr)
        sys.exit(1)

    file_list = None
    if args.file_list:
        try:
            with open(args.file_list, "r") as f:
                file_list = [line for line in f if line.strip()]
        except OSError as e:
            print(f"error: cannot read file list '{args.file_list}': {e}",
                  file=sys.stderr)
            sys.exit(1)

    tests = scan_tests(args.input_dir, file_list)
    tests.sort(key=lambda t: t[0])
    generate(tests, args.output_file)


if __name__ == "__main__":
    main()
