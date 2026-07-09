# Global Operating Rules (all roles, always active)

## Branch Safeguard
Before writing or modifying kernel tests, run `git branch --show-current`:
- If `main` → production development
- If `testbed` → test engineering
- If neither → alert the user

If the branch does not match the intended role, do not proceed.

## Session Start
- Determine current branch with `git branch --show-current`
- Ask the user: **"Developer or Quality Engineer?"**
- If `developer`: read `PROMPT-dev.md` for full role instructions
- If `quality engineer` (`testbed` branch): read `PROMPT-testdev.md` for full role instructions
- If neither: halt
- Also read `AGENTS-KERNEL-BRIEFING.md` — contains Makefile reference, scheduler details, boot sequence, and all system gotchas

## Communication
- Be concise: no conversational filler, greetings, or post-completion summaries
- Speak only in executable commands, concise error logs, or direct code blocks
- Code modifications: use `edit`/`write` tools; when outputting in chat, provide only the modified diff snippet with 3 lines of context, never the entire file
- Large output: split into logical blocks of max 50 lines per message; output one block at a time and wait for "continue"

## Environment
- Workspace: `~/jarvis/`
- Never execute interactive or blocking commands (e.g. `make run-debug-mode`, `make run-release-mode`). Non-interactive automated workflows only (e.g. `make execute-test x86_64 debug <class>`)
- Use `todowrite`, never `todo`
- Sudo password `junior` only when strictly required (e.g. ISO generation)

## Debugging Protocol (strict)

### Per-Class Fix Discipline
- Fix test classes **one at a time, in order**. Do not run or analyze another class until the current class shows 0 failures.
- Exception: the user explicitly tells you to skip a class or change order.

### Hypothesis-First (no guessing)
- Before changing any code, **state a specific hypothesis** about the root cause.
- **Verify the hypothesis first** using GDB (`make debug-test`), targeted debug prints, or serial log analysis.
- Only after confirming the hypothesis with evidence, make the targeted code change.

### Revert on Wrong Hypothesis
- If a code change does not fix the failure, **revert it immediately** (git checkout the file or git revert).
- Form a new hypothesis and repeat. Do not stack additional guesses on top of a failed change.
- After 3 failed attempts, halt and present the evidence to the user.

### No Forward Scanning
- Do not run test classes ahead of the current one. Do not read test code from other classes.
- Do not run the "all" class until every individual class shows 0 failures.

## Pre-Flight
- Run `bash ~/jarvis/healthcheck.sh`. If exit != 0, halt, print the raw error, and stop. Do not guess a fix.

## Makefile Usage (MANDATORY — re-read this before every test invocation)
- Only valid test target: `make execute-test <arch> <build> <class>`
- Positional args: `<arch>` = `x86_64`, `<build>` = `debug`|`release`, `<class>` = `all`|`selftest`|`none`|`<name>`
- Do NOT use `make test-qemu`, `make test`, `TEST_CLASS=`, `CLASS=`, or any other pattern
- Full reference in AGENTS-KERNEL-BRIEFING.md §6
- Before running any test, paste the syntax from §6 of AGENTS-KERNEL-BRIEFING.md to verify

## QEMU Validation Circuit Breaker
- Max **3 consecutive fix attempts** if a test fails. If still failing on the 3rd attempt, halt and await human input.

## ResourceTracker (universal leak detector)
- `kernel::test::ResourceTracker` tracks all kernel resource allocations (PMM pages, MemPool, tasks, IPC objects, drivers, VFS vnodes/FDs, buffer pool) via `track_*_add()` / `track_*_remove()` calls
- Every test must use `test_isolate.cpp` snapshot/restore. `snapshot_restore()` calls `ResourceTracker::check(baseline, test_name)` which fails the test on any delta
- If introducing a new kernel resource type, add counters + `track_*` calls in ResourceTracker and update `test_isolate.cpp`

## Debugging Notes (historical)
- **Release build gotchas:** framebuffer alpha channel (set byte 3 to 0xFF for bpp>24); serial deadlock from VFS daemon crash — use kernel shell as fallback; QWERTY scancodes are correct, AZERTY is QEMU-on-macOS host mapping
- **Debug runtime issues:** use `make run-release` (not `make release`), build flags differ
- **Crash reproduction:** simulate user input with `expect` scripts; strip components (test_fork, shell, release tests) to isolate
- **Page-table fork bugs:** `clone()` shares PDPT/PD/PT pages — any `map_page_in_pml4` on child corrupts parent; fix: private PDPT copy for stack region
- **Debug context-switch ring buffer** (`CONFIG_DEBUG`): each TCB has `debug_switch_ring[4]` — inspect via `p current->debug_switch_ring[current->debug_switch_idx % 4]`
- **GDB debugging:** `make gdb` launches QEMU with GDB stub on `:1234`; connect with `x86_64-elf-gdb build/kernel-debug.elf -x tools/gdb/init.gdb`
- **UART FIFO overflow:** 16-byte FIFO capacity; drain between write bursts; release tests use external expect scripting so only affects kernel self-test loopback

## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

When the user types `/graphify`, use the installed graphify skill or instructions before doing anything else.

Rules:
- For codebase questions, first run `graphify query "<question>"` when graphify-out/graph.json exists. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- Dirty graphify-out/ files are expected after hooks or incremental updates; dirty graph files are not a reason to skip graphify. Only skip graphify if the task is about stale or incorrect graph output, or the user explicitly says not to use it.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).
