#!/usr/bin/env python3
"""Jarvis RTOS — Coding Style Validator.

Usage:
    python3 tools/validate_style.py                  # scan all src/
    python3 tools/validate_style.py --root src/kernel # scan kernel only
    python3 tools/validate_style.py --json            # JSON output
    python3 tools/validate_style.py --quiet           # summary only
"""

import argparse
import json
import os
import re
import sys
from pathlib import Path
from typing import Any


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def config_path() -> Path:
    return Path(__file__).parent / "style_config.json"


def load_config() -> dict[str, Any]:
    cp = config_path()
    if cp.exists():
        return json.loads(cp.read_text())
    return {}


def is_kernel_file(rel_path: str, cfg: dict) -> bool:
    kr = cfg.get("kernel_only_rules", {})
    if not kr.get("enabled", True):
        return False
    includes = kr.get("paths", ["src/kernel/"])
    excludes = kr.get("excludes", ["src/kernel/test/"])
    matched = any(rel_path.startswith(p) for p in includes)
    excluded = any(rel_path.startswith(e) for e in excludes)
    return matched and not excluded


def is_test_file(rel_path: str) -> bool:
    return "/test/" in rel_path or rel_path.startswith("src/kernel/test/")


def get_line(text: str, pos: int) -> int:
    return text[:pos].count("\n") + 1


# ---------------------------------------------------------------------------
# Violation record
# ---------------------------------------------------------------------------

class Violation:
    def __init__(self, file: str, line: int, rule: str, severity: str, message: str):
        self.file = file
        self.line = line
        self.rule = rule
        self.severity = severity
        self.message = message

    def to_dict(self) -> dict:
        return {
            "file": self.file,
            "line": self.line,
            "rule": self.rule,
            "severity": self.severity,
            "message": self.message,
        }

    def __str__(self) -> str:
        sev = "E" if self.severity == "error" else "W"
        return f"  [{sev}] {self.file}:{self.line}  {self.rule} — {self.message}"


# ---------------------------------------------------------------------------
# Base checker
# ---------------------------------------------------------------------------

class Checker:
    name: str = ""
    severity: str = "warning"

    def __init__(self, cfg: dict):
        self.cfg = cfg
        ccfg = self.cfg.get("checks", {}).get(self.name, {})
        self.severity = ccfg.get("severity", self.severity)
        self.violations: list[Violation] = []

    def add(self, file: str, line: int, message: str, rule: str | None = None):
        self.violations.append(
            Violation(file, line, rule or self.name, self.severity, message)
        )

    def check_file(self, rel_path: str, text: str) -> None:
        raise NotImplementedError


# ---------------------------------------------------------------------------
# 1. Naming conventions
# ---------------------------------------------------------------------------

class NamingChecker(Checker):
    name = "naming"

    # Types: PascalCase
    _struct_class_enum = re.compile(
        r"^\s*(struct|class|enum)\s+(\w+)", re.MULTILINE
    )
    _pascal = re.compile(r"^[A-Z][a-zA-Z0-9]*$")

    # Functions: snake_case (catch test macros)
    _func_def = re.compile(
        r"(?:^|\s)(?:static\s+)?(?:inline\s+)?(?:constexpr\s+)?"
        r"(?:\w+(?:::\w+)*\s+[*&]?\s*)(\w+)\s*\(",
        re.MULTILINE,
    )
    _snake = re.compile(r"^[a-z_][a-z0-9_]*$")
    _macro_func = re.compile(r"^(test_|register_|JARVIS_)")

    # Constexpr constants
    _constexpr = re.compile(
        r"(?:^|\s)(?:inline\s+)?constexpr\s+(?:\w+(?:::\w+)*\s+[*&]?\s*)(\w+)\s*[=;]",
        re.MULTILINE,
    )
    _upper_snake = re.compile(r"^[A-Z][A-Z0-9_]*$")

    # Comment line detection
    _comment_line = re.compile(r"^\s*(//|/\*|\*)")

    def check_file(self, rel_path: str, text: str) -> None:
        lines = text.split("\n")

        for i, line in enumerate(lines):
            if self._comment_line.match(line):
                continue

        for m in self._struct_class_enum.finditer(text):
            kw, name = m.group(1), m.group(2)
            if kw == "enum" and name.endswith("Error"):
                continue  # error enums use SCREAMING_SNAKE values
            if not self._pascal.match(name):
                self.add(rel_path, get_line(text, m.start(2)),
                         f"Type '{name}' should be PascalCase")

        for m in self._func_def.finditer(text):
            line_num = get_line(text, m.start(1))
            line_text = lines[line_num - 1] if line_num <= len(lines) else ""
            if self._comment_line.match(line_text):
                continue
            name = m.group(1)
            if name.startswith("operator") or name.startswith("~"):
                continue
            if self._macro_func.match(name) or name == "main":
                continue
            if not self._snake.match(name):
                self.add(rel_path, line_num,
                         f"Function '{name}' should be snake_case")

        for m in self._constexpr.finditer(text):
            line_num = get_line(text, m.start(1))
            line_text = lines[line_num - 1] if line_num <= len(lines) else ""
            if self._comment_line.match(line_text):
                continue
            name = m.group(1)
            if name.startswith("operator"):
                continue
            if not self._upper_snake.match(name):
                self.add(rel_path, line_num,
                         f"Constexpr '{name}' should be SCREAMING_SNAKE")


# ---------------------------------------------------------------------------
# 2. Headers: #pragma once, include order
# ---------------------------------------------------------------------------

class HeadersChecker(Checker):
    name = "headers"

    _include = re.compile(r'#include\s+[<"]([^>"]+)[>"]')

    def check_file(self, rel_path: str, text: str) -> None:
        if not rel_path.endswith(".hpp") and not rel_path.endswith(".h"):
            return
        if not text.startswith("#pragma once") and "//" not in text[:20]:
            self.add(rel_path, 1, "Missing '#pragma once'")

        last_includes: list[str] = []
        for m in self._include.finditer(text):
            inc = m.group(1)
            if inc.startswith(("src/", "kernel/", "lib/")):
                last_includes.append(inc)
            else:
                if any(x.startswith("src/") for x in last_includes):
                    self.add(rel_path, get_line(text, m.start()),
                             f"Include order: system headers should come before local. Saw '{inc}' after local includes")


# ---------------------------------------------------------------------------
# 3. Assertions: ENSURE/ASSERT usage
# ---------------------------------------------------------------------------

class AssertionsChecker(Checker):
    name = "assertions"

    _ASSERT = re.compile(r"\bASSERT\s*\(")
    _ENSURE = re.compile(r"\bENSURE\s*\(")

    def check_file(self, rel_path: str, text: str) -> None:
        pass  # This is a pass-through — we don't enforce specific patterns beyond existing code


# ---------------------------------------------------------------------------
# 4. Memory: no dynamic allocation on real-time paths
# ---------------------------------------------------------------------------

class MemoryChecker(Checker):
    name = "memory"

    _new_delete = re.compile(r"\bnew\s|delete\s|malloc\s*\(|free\s*\(")

    def check_file(self, rel_path: str, text: str) -> None:
        if not is_kernel_file(rel_path, self.cfg):
            return
        if is_test_file(rel_path):
            return
        for m in self._new_delete.finditer(text):
            line = get_line(text, m.start())
            snippet = text[m.start():m.start()+30]
            line_text = text.split('\n')[line - 1].strip()
            if line_text.startswith("//") or line_text.startswith("/*"):
                continue
            # Skip operator new/delete declarations (they define the allocator)
            if any(kw in line_text for kw in ("operator new", "operator delete",
                                               "void* operator", "void operator")):
                continue
            # Skip FdTable::free (file descriptor release, not heap)
            if "free" in snippet and ("::free" in line_text or "FdTable" in line_text):
                continue
            # Skip function/method declarations of free/malloc
            if "free(" in snippet and (line_text.endswith("{") or line_text.endswith(");")):
                continue
            # Skip constructor initializer lists (member initialization like : data(nullptr))
            if "free" in snippet and re.search(r":\s*\w+\s*\(", line_text):
                continue
            # Skip constructor initializer list continuation lines (, member(value))
            if "free" in snippet and re.search(r"^\s*,\s*\w+\s*\(", line_text):
                continue
            self.add(rel_path, line,
                     f"Dynamic allocation on kernel path: '{snippet.strip()}' — use fixed pools instead")


# ---------------------------------------------------------------------------
# 5. Loop bounds
# ---------------------------------------------------------------------------

class LoopBoundsChecker(Checker):
    name = "loop_bounds"

    _while_true = re.compile(r"\bwhile\s*\(\s*(?:true|1)\s*\)")
    _for_semi = re.compile(r"\bfor\s*\(\s*;;\s*\)")

    def check_file(self, rel_path: str, text: str) -> None:
        if not is_kernel_file(rel_path, self.cfg):
            return

        for m in self._while_true.finditer(text):
            self.add(rel_path, get_line(text, m.start()),
                     "Unbounded loop: 'while (true)' without explicit max iterations — use bounded loop with max-count guard")

        for m in self._for_semi.finditer(text):
            self.add(rel_path, get_line(text, m.start()),
                     "Unbounded loop: 'for (;;)' without explicit max iterations — use bounded loop with max-count guard")


# ---------------------------------------------------------------------------
# 6. Forbidden patterns
# ---------------------------------------------------------------------------

class ForbiddenChecker(Checker):
    name = "forbidden"

    _patterns: list[tuple[re.Pattern, str]] = [
        (re.compile(r"\bdynamic_cast\s*<"), "dynamic_cast is forbidden (no RTTI)"),
        (re.compile(r"\btypeid\s*\("), "typeid is forbidden (no RTTI)"),
    ]

    def check_file(self, rel_path: str, text: str) -> None:
        for pat, msg in self._patterns:
            for m in pat.finditer(text):
                self.add(rel_path, get_line(text, m.start()), msg)


# ---------------------------------------------------------------------------
# 7. Test structure
# ---------------------------------------------------------------------------

class TestStructureChecker(Checker):
    name = "test_structure"

    _JARVIS_TEST = re.compile(r"JARVIS_TEST\s*\(\s*(\w+)\s*\)")
    _RUNMODE = re.compile(r"//\s*Runmode\s*:")
    _TESTIDEA = re.compile(r"//\s*Testidea\s*:")
    _INPUT = re.compile(r"//\s*Input\s*:")
    _EXPECT = re.compile(r"//\s*Expect\s*:")
    _DEPENDS = re.compile(r"//\s*Depends\s*:")
    _STUB = re.compile(r"JARVIS_TEST_PASS\s*\(\)")
    _ASSERT = re.compile(
        r"JARVIS_ASSERT|JARVIS_ASSERT_EQ|JARVIS_ASSERT_HEX_EQ"
    )

    def check_file(self, rel_path: str, text: str) -> None:
        if not is_test_file(rel_path):
            return

        for m in self._JARVIS_TEST.finditer(text):
            test_name = m.group(1)
            test_start = m.start()
            test_line = get_line(text, test_start)

            # Check doc-block exists before the test
            before = text[:test_start].rstrip()
            fields_present = {
                "Runmode": self._RUNMODE.search(before),
                "Testidea": self._TESTIDEA.search(before),
                "Input": self._INPUT.search(before),
                "Expect": self._EXPECT.search(before),
                "Depends": self._DEPENDS.search(before),
            }
            missing = [k for k, v in fields_present.items() if not v]
            if missing:
                self.add(rel_path, test_line,
                         f"Test '{test_name}' missing doc-block fields: {', '.join(missing)}")

            # Check if stub-only test contains logic
            body_end = self._find_test_body_end(text, test_start)
            body = text[test_start:body_end]

            stub_only = bool(self._STUB.search(body))
            has_asserts = bool(self._ASSERT.search(body))

            if stub_only and has_asserts:
                self.add(rel_path, test_line,
                         f"Test '{test_name}' is a stub (JARVIS_TEST_PASS only) but contains assertions")

    @staticmethod
    def _find_test_body_end(text: str, start: int) -> int:
        depth = 0
        in_brace = False
        for i in range(start, len(text)):
            ch = text[i]
            if ch == '{':
                if depth == 0:
                    in_brace = True
                depth += 1
            elif ch == '}':
                depth -= 1
                if in_brace and depth == 0:
                    return i + 1
        return len(text)


# ---------------------------------------------------------------------------
# 8. Formatting
# ---------------------------------------------------------------------------

class FormattingChecker(Checker):
    name = "formatting"

    _tab = re.compile(r"^\t", re.MULTILINE)

    _excluded_files = {
        "src/services/terminal/font.hpp",   # font glyph data tables
    }

    def check_file(self, rel_path: str, text: str) -> None:
        if rel_path in self._excluded_files:
            return
        max_line = self.cfg.get("formatting", {}).get("max_line_length", 100)

        for m in self._tab.finditer(text):
            self.add(rel_path, get_line(text, m.start()),
                     "Tab used for indentation — use 4 spaces")

        for i, line in enumerate(text.split("\n"), 1):
            if len(line) > max_line and not line.startswith("//") and not line.startswith("*"):
                if "#include" not in line and "http" not in line:
                    self.add(rel_path, i,
                             f"Line exceeds {max_line} characters ({len(line)})")


# ---------------------------------------------------------------------------
# 9. Const correctness (kernel-only)
# ---------------------------------------------------------------------------

class ConstCorrectnessChecker(Checker):
    name = "const_correctness"

    _member_fn_def = re.compile(
        r"(?:^|\s)(\w+(?:::)?\w+)\s*\([^)]*\)\s*(?:const\s*)?(?:\{|;)",
        re.MULTILINE,
    )

    def check_file(self, rel_path: str, text: str) -> None:
        if not is_kernel_file(rel_path, self.cfg):
            return

        for m in self._member_fn_def.finditer(text):
            pass  # Placeholder: full const-correctness analysis requires semantic analysis


# ---------------------------------------------------------------------------
# 10. References over pointers (kernel-only)
# ---------------------------------------------------------------------------

class RefOverPtrChecker(Checker):
    name = "ref_over_ptr"

    _ptr_param = re.compile(
        r"(?:,\s*|\(\s*)(\w+(?:\s*[*])+\s*\w+)(?:\s*[,)])"
    )

    _excluded_files = {
        "src/kernel/memory/checked_ptr.hpp",      # template wrapper - pointers idiomatic
        "src/kernel/memory/mempool.hpp",           # pool allocator - void* interface
        "src/kernel/memory/mempool.cpp",           # pool allocator - void* interface
        "src/kernel/gcov/gcov_handler.cpp",        # gcov callbacks - void* function pointers
        "src/kernel/vfs/vfs.hpp",                  # VnodeOps function pointer signatures
        "src/kernel/vfs/devfs.cpp",                # VFS callback signatures (buffer params)
        "src/kernel/vfs/initrd_fs.cpp",            # VFS callback signatures (buffer params)
        "src/kernel/vfs/pipe.cpp",                 # VFS callback signatures (buffer params)
        "src/kernel/vfs/procfs.cpp",               # VFS callback signatures (buffer params)
        "src/kernel/task/task.hpp",                # internal TCB child list - nullable pointers
        "src/kernel/task/task.cpp",                # internal TCB child list - nullable pointers
        "src/kernel/task/scheduler.hpp",           # id_table helpers - internal hash table
        "src/kernel/task/scheduler.cpp",           # id_table helpers - internal hash table
        "src/kernel/arch/rtc.cpp",                 # false positive: "century * 100" expression
        "src/kernel/sync/queue.hpp",               # waiter array stores TCB pointers
        "src/kernel/sync/queue.cpp",               # waiter array stores TCB pointers
        "src/kernel/sync/semaphore.hpp",           # waiter array stores TCB pointers
        "src/kernel/sync/semaphore.cpp",           # waiter array stores TCB pointers
    }

    _excluded_params = {
        "regs",              # syscall register save area (assembly context)
        "table",             # VMM page table pointer (arch-specific)
        "func",              # gcov handler function pointer callback
        "private_data",      # VFS private data pointer
        "private_data_",     # VFS private data pointer (alternate naming)
        "out",               # output parameter (e.g., tm* out)
        "buf",               # output buffer parameter (e.g., char* buf)
        "dest",              # destination buffer (e.g., uint8_t* dest)
        "value",             # output value parameter (e.g., uint64_t* value)
    }

    def _is_vnodeops_callback(self, line_text: str, param: str) -> bool:
        """Check if this is a VnodeOps function pointer parameter."""
        return ("read" in line_text or "write" in line_text or "ioctl" in line_text or
                "open" in line_text or "close" in line_text or "mmap" in line_text or
                "stat" in line_text) and ("fn" in param.lower() or "func" in param.lower() or
                "callback" in param.lower() or "op" in param.lower())

    def _is_template_param(self, line_text: str, param: str) -> bool:
        """Check if this is a template parameter where pointer is idiomatic."""
        return ("template" in line_text or "typename" in line_text or
                "checked_ptr" in line_text or "MemPool" in line_text)

    def check_file(self, rel_path: str, text: str) -> None:
        if not is_kernel_file(rel_path, self.cfg):
            return
        if rel_path in self._excluded_files:
            return
        for m in self._ptr_param.finditer(text):
            param = m.group(1).strip()
            name = param.split()[-1]
            if name in self._excluded_params:
                continue
            if name.startswith("out_") or name.startswith("result"):
                continue
            if param.count("*") > 1:
                continue
            line_text = text.split('\n')[get_line(text, m.start(1)) - 1].strip()
            # Skip function pointer declarations inside structs (e.g., void (*func)(T*))
            if '(' in param and ')' in param and '*' in param.split('(')[0]:
                continue
            # Skip VnodeOps callback function pointers
            if self._is_vnodeops_callback(line_text, param):
                continue
            # Skip template code where pointers are idiomatic
            if self._is_template_param(line_text, param):
                continue
            # heuristic: suggest ref if not an output param
            self.add(rel_path, get_line(text, m.start(1)),
                     f"Prefer reference over pointer for non-nullable parameter: '{param.strip()}' — use T& instead of T*")


# ---------------------------------------------------------------------------
# 11. All variables initialized (kernel-only)
# ---------------------------------------------------------------------------

class InitRequiredChecker(Checker):
    name = "init_required"

    def check_file(self, rel_path: str, text: str) -> None:
        if not is_kernel_file(rel_path, self.cfg):
            return

        lines = text.split("\n")
        brace_depth = 0
        in_class = False
        at_class_brace = 0  # brace_depth when entering class/struct

        for i, line in enumerate(lines, 1):
            stripped = line.strip()
            if not stripped or stripped.startswith("//") or stripped.startswith("*"):
                continue

            # Track brace depth and class/struct boundaries
            # Check for class/struct/union declarations
            m_class = re.match(
                r"^\s*(class|struct|union)\s", stripped
            )
            if m_class and not in_class:
                in_class = True
                at_class_brace = brace_depth + stripped.count("{")

            # Update brace depth
            opens = stripped.count("{")
            closes = stripped.count("}")
            brace_depth += opens - closes

            # Check if we've left the class body
            if in_class and brace_depth < at_class_brace:
                in_class = False

            # Skip non-declaration lines
            if any(stripped.startswith(kw) for kw in
                   ("return", "goto ", "case ", "break", "continue",
                    "typedef", "delete ", "extern", "using ")):
                continue

            # Skip lines inside class/struct body (member declarations)
            if in_class:
                continue

            # Detect uninitialized variable: type name;
            m = re.match(
                r"(?:const\s+|volatile\s+)?"
                r"(?:\w+(?:::\w+)*(?:\s*[*&])?\s+)+?"
                r"(\w+)\s*;\s*(?://.*)?$",
                stripped,
            )
            if m:
                name = m.group(1)
                if name == name.upper():
                    continue  # skip global-like constants
                if name in ("true", "false", "nullptr", "NULL", "stdout",
                            "stdin", "stderr"):
                    continue  # skip keywords mistaken as var names
                self.add(rel_path, i,
                         f"Variable '{name}' declared without initialization — use '= ...', '{{}}', or '(...)'")


# ---------------------------------------------------------------------------
# 12. Constructor initializer lists (kernel-only)
# ---------------------------------------------------------------------------

class CtorInitListChecker(Checker):
    name = "ctor_init_list"

    _ctor_body = re.compile(
        r"(\w+(?:::)?\w+)::(\w+)\s*\([^)]*\)\s*\{"
    )
    _member_assign = re.compile(
        r"(?:this->)?(\w+)\s*=\s*(.+?);"
    )

    def check_file(self, rel_path: str, text: str) -> None:
        if not is_kernel_file(rel_path, self.cfg):
            return

        for m in self._ctor_body.finditer(text):
            start = m.start()
            end = self._find_brace_end(text, m.end() - 1)
            body = text[m.end() - 1:end]

            # Check if there's a member init list (has colon before body)
            before_body = text[start:m.end()]
            has_init_list = ":" in before_body.split("{")[0] and \
                before_body.split("{")[0].count(":") > before_body.split("{")[0].count("?")

            if not has_init_list:
                for am in self._member_assign.finditer(body):
                    lhs = am.group(1)
                    if lhs != lhs.upper():
                        self.add(rel_path, get_line(text, m.start()),
                                 f"Constructor initializes '{lhs}' in body — use member initializer list instead")

    @staticmethod
    def _find_brace_end(text: str, start: int) -> int:
        depth = 0
        for i in range(start, len(text)):
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
                if depth == 0:
                    return i + 1
        return len(text)


# ---------------------------------------------------------------------------
# 13. Sentinel enums (kernel-only)
# ---------------------------------------------------------------------------

class SentinelEnumChecker(Checker):
    name = "sentinel_enum"

    _raw_minus_one = re.compile(r"(?:!=|==)\s*(-1)\b(?!\s*\))")
    _raw_minus_one_fn = re.compile(
        r"\b(isEmpty|is_empty|has_data|has_buffers|is_valid)\s*\([^)]*\)\s*\{[^}]*?(?:!=|==)\s*-1",
        re.DOTALL,
    )

    def check_file(self, rel_path: str, text: str) -> None:
        if not is_kernel_file(rel_path, self.cfg):
            return

        for m in self._raw_minus_one.finditer(text):
            line = get_line(text, m.start())
            line_text = text.split('\n')[line - 1]
            if "//" in line_text and m.group(1) in line_text.split("//")[1]:
                continue
            # Check if there's a named constant in scope
            self.add(rel_path, line,
                     f"Raw '-1' comparison — use a named sentinel constant (e.g., LIST_EMPTY) instead")


# ---------------------------------------------------------------------------
# 14. Descriptive names (kernel-only)
# ---------------------------------------------------------------------------

class DescriptiveNamesChecker(Checker):
    name = "descriptive_names"

    _var_decl = re.compile(
        r"(?:^|\s)((?:const\s+|static\s+|inline\s+)*)"
        r"(?:(?:[a-z_][a-z0-9_]*|[A-Z][a-zA-Z0-9]*)(?:::[a-z_][a-z0-9_]*|[A-Z][a-zA-Z0-9]*)*)\s+"
        r"([a-zA-Z_][a-zA-Z0-9_]*)\s*(?:[=;({])",
        re.MULTILINE,
    )
    # Keywords that should not be flagged as variable names
    _keywords = {
        "if", "else", "while", "for", "do", "switch", "case", "default",
        "return", "break", "continue", "goto", "sizeof", "alignof",
        "typeof", "static_assert", "thread_local", "constexpr", "decltype",
        "new", "delete", "this", "true", "false", "nullptr",
    }
    _fn_param = re.compile(
        r"(?:,\s*|\(\s*)((?:const\s+)?[a-zA-Z_][a-zA-Z0-9_]*(?:::[a-zA-Z_][a-zA-Z0-9_]*)*(?:\s*[*&])?\s+)([a-zA-Z_][a-zA-Z0-9_]*)(?:\s*[=,);])"
    )
    _loop_var = re.compile(
        r"for\s*\(\s*(?:\w+(?:::\w+)*)\s+([a-z])\s*(?:\s*=|:)"
    )

    def check_file(self, rel_path: str, text: str) -> None:
        if not is_kernel_file(rel_path, self.cfg):
            return

        cfg_dn = self.cfg.get("descriptive_names", {})
        blocklist = set(cfg_dn.get("blocklist", ["t", "v", "val", "tmp", "temp", "ptr", "p"]))
        allow_loop = set(cfg_dn.get("allow_loop_indices", ["i", "j", "k", "idx"]))
        allow_common = set(cfg_dn.get("allow_common_abbrevs", ["id"]))
        min_len = cfg_dn.get("min_length", 3)
        max_loop_lines = cfg_dn.get("max_loop_body_lines", 5)

        lines = text.split("\n")

        for m in self._var_decl.finditer(text):
            name = m.group(2)
            decl_line = get_line(text, m.start(2))

            # Skip C++ keywords that might be caught by the regex
            if name in self._keywords:
                continue

            # Check for blocklisted names
            if name in blocklist:
                self.add(rel_path, decl_line,
                         f"Blocklisted variable name '{name}' — use a descriptive name")

            # Check minimum length (but allow 'i', 'j', 'k' in tight loops, and common abbrevs)
            if len(name) < min_len and name not in allow_loop and name not in allow_common:
                self.add(rel_path, decl_line,
                         f"Variable name '{name}' is too short (min {min_len} chars)")

            # Check single-char declarations that are NOT loop indices or common abbrevs
            if len(name) == 1 and name not in allow_loop and name not in allow_common:
                self.add(rel_path, decl_line,
                         f"Single-character variable '{name}' — use a descriptive name")

        for m in self._fn_param.finditer(text):
            name = m.group(2)
            param_line = get_line(text, m.start(2))
            if name in blocklist:
                self.add(rel_path, param_line,
                         f"Blocklisted parameter name '{name}' — use a descriptive name")
            if len(name) < min_len and name not in allow_loop and name not in allow_common:
                self.add(rel_path, param_line,
                         f"Parameter name '{name}' is too short (min {min_len} chars)")

        for m in self._loop_var.finditer(text):
            ch = m.group(1)
            if ch not in allow_loop:
                loop_line = get_line(text, m.start(1))
                # Count lines in loop body
                body_start = text.index("{", m.end()) + 1
                body_end = self._find_brace_end(text, body_start - 1)
                body_lines = text[body_start:body_end - 1].count("\n")
                if body_lines > max_loop_lines:
                    self.add(rel_path, loop_line,
                             f"Loop index '{ch}' used in large loop ({body_lines} lines) — use descriptive name")

    @staticmethod
    def _find_brace_end(text: str, start: int) -> int:
        depth = 0
        for i in range(start, len(text)):
            if text[i] == '{':
                depth += 1
            elif text[i] == '}':
                depth -= 1
                if depth == 0:
                    return i + 1
        return len(text)


# ---------------------------------------------------------------------------
# 15. No const_cast (kernel-only)
# ---------------------------------------------------------------------------

class NoConstCastChecker(Checker):
    name = "no_const_cast"

    _const_cast = re.compile(r"\bconst_cast\s*<")

    def check_file(self, rel_path: str, text: str) -> None:
        if not is_kernel_file(rel_path, self.cfg):
            return

        for m in self._const_cast.finditer(text):
            line = get_line(text, m.start())
            line_text = text.split('\n')[line - 1].strip()
            if line_text.startswith("//") or line_text.startswith("*"):
                continue
            self.add(rel_path, line,
                     "const_cast is forbidden — use 'mutable' or redesign to avoid const modification")


# ---------------------------------------------------------------------------
# Scanner
# ---------------------------------------------------------------------------

def scan_file(rel_path: str, text: str, checkers: list[Checker]) -> list[Violation]:
    all_violations: list[Violation] = []
    for c in checkers:
        c.violations.clear()
        c.check_file(rel_path, text)
        all_violations.extend(c.violations)
    return all_violations


def collect_source_files(root: str) -> list[tuple[str, str]]:
    files: list[tuple[str, str]] = []
    root_path = Path(root).resolve()
    workspace = Path.cwd().resolve()

    for path in root_path.rglob("*"):
        if path.suffix not in (".cpp", ".hpp", ".h", ".c", ".S", ".asm"):
            continue
        if ".git" in path.parts or "build" in path.parts or "obj" in path.parts:
            continue
        # Skip test files
        if "test" in path.parts:
            continue
        try:
            rel = str(path.relative_to(workspace))
            text = path.read_text()
            files.append((rel, text))
        except (OSError, UnicodeDecodeError, ValueError):
            pass
    return files


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description="Jarvis RTOS — Coding Style Validator")
    parser.add_argument("--root", default="src", help="Root directory to scan (default: src)")
    parser.add_argument("--json", action="store_true", help="Output JSON")
    parser.add_argument("--quiet", action="store_true", help="Summary only (no individual violations)")
    parser.add_argument("--severity", choices=["error", "warning"],
                        help="Minimum severity to report (default: report both)")
    args = parser.parse_args()

    cfg = load_config()
    checkers: list[Checker] = [
        NamingChecker(cfg),
        HeadersChecker(cfg),
        AssertionsChecker(cfg),
        MemoryChecker(cfg),
        LoopBoundsChecker(cfg),
        ForbiddenChecker(cfg),
        TestStructureChecker(cfg),
        FormattingChecker(cfg),
        ConstCorrectnessChecker(cfg),
        RefOverPtrChecker(cfg),
        InitRequiredChecker(cfg),
        CtorInitListChecker(cfg),
        SentinelEnumChecker(cfg),
        DescriptiveNamesChecker(cfg),
        NoConstCastChecker(cfg),
    ]

    root = os.path.abspath(args.root)
    if not os.path.isdir(root):
        print(f"Error: directory '{root}' not found", file=sys.stderr)
        return 2

    files = collect_source_files(root)
    all_violations: list[Violation] = []
    for rel_path, text in files:
        all_violations.extend(scan_file(rel_path, text, checkers))

    # Filter by severity
    min_sev = args.severity
    if min_sev:
        all_violations = [v for v in all_violations if v.severity == min_sev]

    # Sort by file, then line
    all_violations.sort(key=lambda v: (v.file, v.line))

    errors = [v for v in all_violations if v.severity == "error"]
    warnings = [v for v in all_violations if v.severity == "warning"]

    if args.json:
        report = {
            "summary": {
                "files_scanned": len(files),
                "violations": len(all_violations),
                "errors": len(errors),
                "warnings": len(warnings),
            },
            "violations": [v.to_dict() for v in all_violations],
        }
        print(json.dumps(report, indent=2))
    else:
        if not args.quiet:
            for v in all_violations:
                print(v)

        print(f"\n{'=' * 60}")
        print(f"  Files scanned: {len(files)}")
        print(f"  Total violations: {len(all_violations)}")
        print(f"  Errors: {len(errors)}")
        print(f"  Warnings: {len(warnings)}")
        print(f"{'=' * 60}")

    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())