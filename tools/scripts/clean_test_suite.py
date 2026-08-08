#!/usr/bin/env python3
"""
clean_test_suite.py -- automated ROADMAP v0.3.13 test-suite refactor.

Transformations applied to every *.cpp under src/kernel/test/:

  T0-1  Replace the raw teardown sequence
          Scheduler::remove_task(*t); t->cleanup(); <delete t | MemPool::free(t)>
        (and whitespace / same-line layout variants, where the task is named
        by a simple identifier) with the canonical helper
          kernel::test::terminate_and_drain(*t);
        The helper is the sanctioned Rule-4 reclaim path
        (test_sched_helpers.hpp); the raw sequence double-frees a block the
        trampoline already routed to the zombie list and only self-heals via
        the magic-guard.

  T1-1  Move trailing JARVIS_ASSERT* statements so they execute strictly
        AFTER the terminate_and_drain() / drain_zombie_list() cleanup hook
        at the end of a test case (Rule-5: asserts `return` on failure and
        would otherwise leak/hang the class).  To keep moved asserts free of
        use-after-free on reclaimed tasks, any task-pointer member access
        `VAR->field` of a task terminated by an adjacent hook is hoisted into
        a local `const auto VAR_field = VAR->field;` captured immediately
        before the hook, and the assert is rewritten to use the local.
        Hooks, comments, and blank lines are preserved in their original
        position; only the assert statements are relocated.

Idempotent: already-canonical sites are left untouched.  An include for
test_sched_helpers.hpp is added only when terminate_and_drain is introduced
into a file that does not already include it.

Usage:
    python3 tools/scripts/clean_test_suite.py [--test-dir DIR] [--dry-run]
"""

from __future__ import annotations

import argparse
import glob
import os
import re
import sys

# ---------------------------------------------------------------------------
# T0-1: raw teardown -> terminate_and_drain
# ---------------------------------------------------------------------------

# Matches the 3-statement teardown.  Group 1 = remove_task argument exactly
# as written (e.g. `*t`); group 2 = the bare variable name, which must appear
# identically in all three statements (backreference \\2).  The free may be
# spelled `delete t` or `MemPool::free(t)` (TCB allocations route to MemPool
# in either spelling).  Only simple identifiers are handled (array/field
# indexed teardowns like remove_task(*load[i]) are left untouched).
TEARDOWN_RE = re.compile(
    r"Scheduler::remove_task\(\s*(\*?\s*(\w+))\s*\)\s*;"
    r"\s*(?:\n\s*)?\2\s*->\s*cleanup\s*\(\s*\)\s*;"
    r"\s*(?:\n\s*)?(?:delete\s+\2|MemPool::free\(\s*\2\s*\))\s*;"
)

TEARDOWN_REPLACEMENT = r"kernel::test::terminate_and_drain(\1);"

# Matches the BATCH-form teardown: N consecutive `remove_task(*X)` lines
# followed by the same N `X->cleanup()` lines and then N
# `delete X | MemPool::free(X)` lines, with matching variable names.  This is
# the grouped layout used by the FPU suite (test_fpu.cpp, test_fpu_multi.cpp,
# test_fpu_sse.cpp, test_fpu_xmm_all.cpp).  Each (remove_task, cleanup, free)
# triple is replaced by terminate_and_drain(*X).
BATCH_REMOVE_RE = re.compile(r"\s*Scheduler::remove_task\(\s*\*\s*(\w+)\s*\)\s*;")
BATCH_CLEANUP_RE = re.compile(r"\s*(\w+)\s*->\s*cleanup\s*\(\s*\)\s*;")
BATCH_FREE_RE = re.compile(r"\s*(?:delete\s+(\w+)|MemPool::free\(\s*(\w+)\s*\))\s*;")


def substitute_batch_teardown(text: str):
    """Replace batch-form teardown blocks with per-task terminate_and_drain.

    Returns (new_text, count).  The match requires an exact 3*N line group
    (ignoring only blank lines) where the variable sets agree across the three
    phases; otherwise the block is left untouched.
    """
    lines = text.split("\n")
    out = []
    count = 0
    i = 0
    n = len(lines)
    while i < n:
        # Try to start a batch block at line i.
        if BATCH_REMOVE_RE.match(lines[i]):
            rm = []
            j = i
            m = BATCH_REMOVE_RE.match(lines[j])
            while m is not None:
                rm.append(m.group(1))
                j += 1
                m = BATCH_REMOVE_RE.match(lines[j]) if j < n else None
            if len(rm) < 2:
                out.append(lines[i])
                i += 1
                continue
            # Skip blank lines then expect cleanup block.
            k = j
            while k < n and is_blank(lines[k]):
                k += 1
            cl = []
            kk = k
            m = BATCH_CLEANUP_RE.match(lines[kk]) if kk < n else None
            while m is not None:
                cl.append(m.group(1))
                kk += 1
                m = BATCH_CLEANUP_RE.match(lines[kk]) if kk < n else None
            if cl != rm:
                out.append(lines[i])
                i += 1
                continue
            # Skip blank lines then expect free block.
            k2 = kk
            while k2 < n and is_blank(lines[k2]):
                k2 += 1
            fr = []
            kk2 = k2
            m = BATCH_FREE_RE.match(lines[kk2]) if kk2 < n else None
            while m is not None:
                fr.append(m.group(1) if m.group(1) else m.group(2))
                kk2 += 1
                m = BATCH_FREE_RE.match(lines[kk2]) if kk2 < n else None
            if fr != rm:
                out.append(lines[i])
                i += 1
                continue
            # Confirm identical indentation across the block.
            indents = {len(ln) - len(ln.lstrip()) for ln in lines[i:kk2]}
            indent = "    " if (indents and min(indents) >= 4) else ""
            for var in rm:
                out.append(f"{indent}kernel::test::terminate_and_drain(*{var});")
            count += len(rm)
            i = kk2
            continue
        out.append(lines[i])
        i += 1
    return "\n".join(out), count

# ---------------------------------------------------------------------------
# T1-1: trailing-assert reorder helpers
# ---------------------------------------------------------------------------

ASSERT_START_RE = re.compile(r"^\s*JARVIS_ASSERT(?:_\w+)?\b")
HOOK_RE = re.compile(
    r"(?:kernel::test::)?terminate_and_drain\s*\(|"
    r"(?:Scheduler::)?drain_zombie_list\s*\("
)
FREED_VAR_RE = re.compile(r"terminate_and_drain\(\s*\*\s*(\w+)\s*\)")
MEMBER_ACCESS_RE = re.compile(r"\b(\w+)\s*->\s*(\w+)\b")
CAST_RE = re.compile(r"(?:static|reinterpret|const|dynamic)_cast\s*<[^>]*>\s*\([^)]*\)")


def is_relocatable_assert(text: str) -> bool:
    """True if the assert may safely run AFTER a drain/terminate hook.

    Only the assert's ARGUMENT body is inspected (the leading macro name and
    its own opening parenthesis are ignored).  The body must reference no live
    task/scheduler state that the drain mutates:
      - no `->` dereference (a remaining deref of a reclaimed task would be a
        use-after-free once the hook frees the TCB);
      - no function/method call (zombie_count(), find_task(), is_locked(),
        owner(), etc. observe state the drain changes);
      - no Scheduler:: call.
    Globals / statics / captured locals (including hoisted VAR->field values)
    are fine.
    """
    m = re.match(r"\s*JARVIS_ASSERT(?:_\w+)?\s*\((.*)\)\s*;?\s*$", text, re.DOTALL)
    if not m:
        return False
    body = m.group(1)
    if "->" in body:
        return False
    if "Scheduler::" in body:
        return False
    stripped = CAST_RE.sub("1", body)
    if re.search(r"\b\w+\s*\(", stripped):
        return False
    return True

BLANK_RE = re.compile(r"^\s*$")
COMMENT_RE = re.compile(r"^\s*(?://|/\*|\*)")


def is_blank(line: str) -> bool:
    return bool(BLANK_RE.match(line))


def is_comment(line: str) -> bool:
    return bool(COMMENT_RE.match(line))


def find_test_body_ranges(lines):
    """Return a list of (start_line, end_line) for each test-case body.

    A test case is introduced by JARVIS_TEST( / JARVIS_TEST_SUITE( /
    TEST_CLASS( and its function body opens with the next '{' that raises the
    brace depth; it closes at the matching '}'.  Brace-depth tracking handles
    nested braces (lambdas, if/for).
    """
    ranges = []
    i = 0
    n = len(lines)
    while i < n:
        if re.search(r"\b(?:JARVIS_TEST|JARVIS_TEST_SUITE|TEST_CLASS)\s*\(", lines[i]):
            depth = 0
            j = i
            opened = False
            while j < n:
                depth += lines[j].count("{") - lines[j].count("}")
                if depth > 0:
                    opened = True
                if opened and depth == 0:
                    ranges.append((i, j))
                    i = j + 1
                    break
                j += 1
            else:
                break
        else:
            i += 1
    return ranges


def split_body_statements(body_lines):
    """Split a test body (lines WITHOUT the outer braces) into top-level blocks.

    Returns a list of dicts: {kind: blank|comment|stmt, start, end, stmt?}.
    depth is relative to the body's own opening brace; only depth-0
    statements are emitted (lambda bodies, if/for bodies stay folded inside
    their parent statement).
    """
    blocks = []
    i = 0
    n = len(body_lines)
    depth = 0
    stmt_start = None
    stmt_paren = 0
    while i < n:
        line = body_lines[i]
        if is_blank(line):
            if stmt_start is None:
                blocks.append({"kind": "blank", "start": i, "end": i})
            i += 1
            continue
        if is_comment(line) and stmt_start is None:
            blocks.append({"kind": "comment", "start": i, "end": i})
            i += 1
            continue

        if stmt_start is None:
            stmt_start = i
            stmt_paren = 0

        depth += line.count("{") - line.count("}")
        stmt_paren += line.count("(") - line.count(")")

        if depth == 0 and stmt_paren <= 0 and line.rstrip().endswith(";"):
            text = "\n".join(body_lines[stmt_start : i + 1])
            blocks.append({
                "kind": "stmt",
                "start": stmt_start,
                "end": i,
                "stmt": text,
            })
            stmt_start = None
        i += 1

    if stmt_start is not None and stmt_start < n:
        text = "\n".join(body_lines[stmt_start:n])
        blocks.append({
            "kind": "stmt",
            "start": stmt_start,
            "end": n - 1,
            "stmt": text,
        })
    return blocks


def reorder_body(body_lines):
    """Apply the T1-1 trailing-assert reorder to one test body.

    body_lines is the body WITHOUT its outer braces.  Returns a new list of
    lines, or the original list if no reorder was needed.

    Only asserts that are SAFE to run after the cleanup hook are relocated:
    asserts referencing live task/scheduler state (`VAR->field`, calls such as
    zombie_count()/find_task()/is_locked()) stay in place before the hook.
    Member accesses of tasks freed by a terminate_and_drain() hook are hoisted
    into `const auto VAR_field = VAR->field;` locals captured before the hook,
    and the assert is rewritten to use the local so it becomes relocatable.
    """
    if not any(HOOK_RE.search(ln) for ln in body_lines):
        return body_lines

    blocks = split_body_statements(body_lines)

    def is_assert(b):
        return b["kind"] == "stmt" and ASSERT_START_RE.match(b["stmt"].split("\n")[0])

    def is_hook(b):
        return b["kind"] == "stmt" and HOOK_RE.search(b["stmt"])

    hook_idxs = [k for k, b in enumerate(blocks) if is_hook(b)]
    if not hook_idxs:
        return body_lines
    last_hook = hook_idxs[-1]

    # Collect the trailing region immediately before the last hook: a run of
    # assert statements (plus hooks and blanks/comments between them).
    # Walking upward from the last hook, include blocks until a non-assert,
    # non-hook, non-blank, non-comment statement is found.
    region = [last_hook]
    k = last_hook - 1
    found_assert = False
    while k >= 0:
        b = blocks[k]
        if b["kind"] in ("blank", "comment"):
            k -= 1
            continue
        if is_hook(b):
            region.insert(0, k)
            k -= 1
            continue
        if is_assert(b):
            region.insert(0, k)
            found_assert = True
            k -= 1
            continue
        break

    if not found_assert:
        return body_lines

    first = region[0]
    last = last_hook

    # Freed task vars from ALL terminate_and_drain hooks in this body.
    freed_vars = set()
    for k in hook_idxs:
        freed_vars.update(FREED_VAR_RE.findall(blocks[k]["stmt"]))

    # For each trailing assert, decide whether it can move after the hook.
    # First hoist member accesses of freed tasks into locals.  The rewritten
    # text (using hoisted locals) is only used for RELOCATABLE asserts; asserts
    # that stay before the hook keep their original `VAR->field` text (the
    # hoist declaration lives after them, so rewriting would break scope).
    assert_plan = {}  # block_idx -> (orig_text, hoisted_text, relocatable)
    hoists = []
    seen = set()
    for idx in range(first, last + 1):
        b = blocks[idx]
        if not is_assert(b):
            continue
        orig_text = b["stmt"]
        text_hoisted = orig_text
        for var, field in MEMBER_ACCESS_RE.findall(orig_text):
            if var in freed_vars:
                key = (var, field)
                if key not in seen:
                    seen.add(key)
                    hoists.append(key)
                text_hoisted = re.sub(
                    rf"\b{var}\s*->\s*{field}\b", f"{var}_{field}", text_hoisted
                )
        reloc = is_relocatable_assert(text_hoisted)
        assert_plan[idx] = (orig_text, text_hoisted, reloc)

    if not any(reloc for _, _, reloc in assert_plan.values()):
        return body_lines

    # Indentation for the hoisted locals: match the first hook/assert level.
    indent = ""
    for idx in range(first, last + 1):
        b = blocks[idx]
        if b["kind"] == "stmt":
            first_line = b["stmt"].split("\n")[0]
            stripped = first_line.lstrip()
            indent = first_line[: len(first_line) - len(stripped)]
            break

    # Only emit hoists actually referenced by a RELOCATABLE assert (an
    # unused local would trip -Werror=unused-variable).  Non-relocatable
    # asserts keep their original `VAR->field` form and read live state
    # before the hook.
    used_hoists = set()
    for _, text_hoisted, reloc in assert_plan.values():
        if reloc:
            for var, field in hoists:
                if f"{var}_{field}" in text_hoisted:
                    used_hoists.add((var, field))

    # Emit the region: all non-assert blocks (hooks, comments, blanks) and the
    # non-relocatable asserts stay in their original order; the hoisted captures
    # go just before the first hook; relocatable asserts move after the last hook.
    out_region = []
    hoists_emitted = False

    def _emit_hoists():
        nonlocal hoists_emitted
        if not hoists_emitted:
            for var, field in hoists:
                if (var, field) in used_hoists:
                    out_region.append(f"{indent}const auto {var}_{field} = {var}->{field};")
            hoists_emitted = True

    for idx in range(first, last + 1):
        b = blocks[idx]
        if b["kind"] in ("blank", "comment"):
            out_region.extend(body_lines[b["start"] : b["end"] + 1])
        elif is_hook(b):
            _emit_hoists()
            out_region.extend(b["stmt"].split("\n"))
        elif is_assert(b):
            orig_text, text_hoisted, reloc = assert_plan[idx]
            if not reloc:  # non-relocatable asserts stay before the hook
                out_region.extend(orig_text.split("\n"))
        # relocatable asserts: deferred to the end

    _emit_hoists()

    for idx in range(first, last + 1):
        b = blocks[idx]
        if is_assert(b):
            orig_text, text_hoisted, reloc = assert_plan[idx]
            if reloc:
                out_region.extend(text_hoisted.split("\n"))

    start = blocks[first]["start"]
    end = blocks[last]["end"]
    out = list(body_lines)
    out[start : end + 1] = out_region
    return out


def apply_t11(text: str) -> str:
    """Apply the T1-1 reorder to a whole file's text."""
    lines = text.split("\n")
    ranges = find_test_body_ranges(lines)
    if not ranges:
        return text
    for start, end in sorted(ranges, key=lambda r: r[0], reverse=True):
        body = lines[start + 1 : end]
        new_body = reorder_body(body)
        if new_body is not body:
            lines[start + 1 : end] = new_body
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# include management
# ---------------------------------------------------------------------------

INCLUDE_LOCAL = '#include "test_sched_helpers.hpp"\n'
INCLUDE_ANGLE = "#include <kernel/test/test_sched_helpers.hpp>\n"
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]')


def ensure_helper_include(text: str, has_angle_includes: bool) -> str:
    if "test_sched_helpers.hpp" in text:
        return text
    inc = INCLUDE_ANGLE if has_angle_includes else INCLUDE_LOCAL
    lines = text.split("\n")
    insert_at = 0
    for i, ln in enumerate(lines):
        if INCLUDE_RE.match(ln):
            insert_at = i + 1
    lines.insert(insert_at, inc.rstrip("\n"))
    return "\n".join(lines)


# ---------------------------------------------------------------------------
# main driver
# ---------------------------------------------------------------------------


def process_file(path: str, dry_run: bool):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    orig = text

    text, n_subs = TEARDOWN_RE.subn(TEARDOWN_REPLACEMENT, text)
    text, n_batch = substitute_batch_teardown(text)
    n_subs += n_batch
    text = apply_t11(text)

    if "kernel::test::terminate_and_drain" in text:
        has_angle = "#include <kernel/test/" in text
        text = ensure_helper_include(text, has_angle)

    if text == orig:
        return {"file": path, "changed": False, "teardowns": n_subs}

    if not dry_run:
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
    return {"file": path, "changed": True, "teardowns": n_subs}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--test-dir", default="src/kernel/test",
                    help="test source directory (default: src/kernel/test)")
    ap.add_argument("--dry-run", action="store_true",
                    help="report what would change without writing")
    args = ap.parse_args()

    if not os.path.isdir(args.test_dir):
        print(f"ERROR: test dir not found: {args.test_dir}", file=sys.stderr)
        return 2

    files = sorted(glob.glob(os.path.join(args.test_dir, "*.cpp")))
    if not files:
        print(f"ERROR: no *.cpp found under {args.test_dir}", file=sys.stderr)
        return 2

    total_teardowns = 0
    changed = 0
    for path in files:
        res = process_file(path, args.dry_run)
        total_teardowns += res["teardowns"]
        if res["changed"]:
            changed += 1
            tag = "[DRY] " if args.dry_run else "[EDIT]"
            print(f"{tag} {res['file']}  teardown_subs={res['teardowns']}")

    print(
        f"\nSummary: {changed} files touched, {total_teardowns} "
        f"teardown substitutions ({'dry run' if args.dry_run else 'written'})."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
