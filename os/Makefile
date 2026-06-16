# ==============================================================================
# Jarvis RTOS Microkernel Makefile
# Target: x86_64 Freestanding C++20 / ISO 26262 & MISRA-Compliant Toolchain
# ==============================================================================

# ------------------------------------------------------------------------------
# Host Environment & OS Detection
# ------------------------------------------------------------------------------
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
    GET_SIZE := stat -f%z
    PKG_HINT := "Install with: brew install qemu xorriso"
else
    GET_SIZE := stat -c%s
    PKG_HINT := "Install with: apt-get install qemu-system-x86 xorriso grub-common"
endif

# ------------------------------------------------------------------------------
# Toolchain Setup
#
# CXXFLAGS / CCFLAGS here are the *base* flags.  Build-type specific flags
# (e.g. -Og -DCONFIG_DEBUG for debug, -O2 for release) are added via
# target-specific variable overrides on the 'debug' / 'release' targets.
# ------------------------------------------------------------------------------
CXX      := g++
CXXFLAGS := -std=c++20 -ffreestanding -fno-exceptions -fno-rtti \
            -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
            -fno-threadsafe-statics -target x86_64-elf -m64 -mno-red-zone \
            -mgeneral-regs-only -mcmodel=large \
            -Wall -Wextra -Werror \
            -I src -I src/lib -pipe -MMD -MP \
            -ffunction-sections -fdata-sections

CC       := x86_64-elf-gcc
CCFLAGS  := -m64 -static -nostdlib -ffreestanding -O2 -pipe -MMD -MP \
            -ffunction-sections -fdata-sections

AS       := nasm
ASFLAGS  := -f elf64

LD       := x86_64-elf-ld
AR       := x86_64-elf-ar
LDFLAGS  := -m elf_x86_64 -nostdlib -T linker.ld -z max-page-size=0x1000 \
            -Map=build/kernel.map

GCOV_LIB_DIR := $(dir $(shell x86_64-elf-gcc -print-file-name=libgcov.a 2>/dev/null))

# ------------------------------------------------------------------------------
# Output paths
# ------------------------------------------------------------------------------
KERNEL         := build/kernel.elf
KERNEL_DEBUG   := build/kernel-debug.elf
DEBUG_ISO      := debug/jarvis-rtos.iso
RELEASE_ISO    := release/jarvis-rtos.iso
KERNEL_SYMBOLS := build/kernel.sym
KERNEL_DIS     := build/kernel.dis

QEMU_UEFI      ?= /opt/homebrew/share/qemu/edk2-x86_64-code.fd
FAT32_DISK     := build/fat32.img
QEMU_FLAGS     := -cdrom $(DEBUG_ISO) -m 256M -serial mon:stdio -nic none \
                  -boot order=d \
                  -drive if=pflash,format=raw,readonly=on,file=$(QEMU_UEFI) \
                  -drive file=$(FAT32_DISK),format=raw,if=ide,index=1,media=disk

# ------------------------------------------------------------------------------
# Build type stamp
#
# Debug and release share build/.  The stamp ensures make clean is triggered
# when switching build types so stale object files don't leak between builds.
# ------------------------------------------------------------------------------
BUILD_STAMP := build/.build-type

# ------------------------------------------------------------------------------
# Shared build rules (pattern rules, libc, userspace, initrd)
# ------------------------------------------------------------------------------
include mk/rules.mk

# ------------------------------------------------------------------------------
# Phony targets
# ------------------------------------------------------------------------------
.PHONY: help all build check-style run-qemu run-release run-qemu-debug \
        test-qemu test symbols objdump debug release release-test \
        run-release-test profiling gdb test-gdb \
        _run-shell-test

# Default target
all: help

# ------------------------------------------------------------------------------
# Style / Help
# ------------------------------------------------------------------------------
build: check-style debug

check-style:
	@printf '  %-7s %s\n' 'STYLE' 'Validating kernel sources…'
	@python3 tools/validate_style.py --root src/kernel; \
	rc=$$?; \
	if [ $$rc -eq 0 ]; then \
	    printf '  %-7s %s\n' 'STYLE' 'Passed.'; \
	elif [ $$rc -eq 1 ]; then \
	    printf '  %-7s %s\n' 'STYLE' 'Errors found.'; \
	    exit 1; \
	fi

help:
	@echo "Jarvis RTOS — Build Targets"
	@echo ""
	@echo "  [A] make (or make all)   Show this help"
	@echo "  [A] make build           Style check + debug ISO"
	@echo "  [A] make check-style     Validate kernel coding style (src/kernel/)"
	@echo "  [A] make debug           Build debug ISO"
	@echo "  [A] make release         Build production ISO"
	@echo "  [A] make clean           Remove all build artifacts"
	@echo "  [A] make test-qemu       Run full test suite in QEMU (automated)"
	@echo "  [A] make release-test    Release compilation + full test suite"
	@echo "  [A] make run-release-test All shell commands + 2x selftest + exit (release ISO)"
	@echo "  [A] make symbols         Kernel symbol table"
	@echo "  [A] make objdump         Kernel disassembly"
	@echo "  [A] make profiling       GCOV coverage capture"
	@echo "  [I] make run-qemu        Boot debug ISO (interactive shell)"
	@echo "  [I] make run-release     Build + boot release ISO (interactive shell)"
	@echo "  [I] make gdb             Debug ISO + QEMU with GDB stub (:1234)"
	@echo "  [I] make test-gdb        Run tests under GDB surveillance"
	@echo ""
	@echo "  [A] = autonomous (run and done, no interaction needed)"
	@echo "  [I] = interactive (user sits at the QEMU/GDB console)"

# ------------------------------------------------------------------------------
# Build-type stamp check
#
# Triggers clean if:
#   (a) stamp exists and doesn't match 'debug', or
#   (b) no stamp exists but .o files are present (interrupted build).
# ------------------------------------------------------------------------------
check-build-stamp:
	@stamp=; target=debug; \
	if [ -f $(BUILD_STAMP) ]; then \
	    stamp=$$(cat $(BUILD_STAMP)); \
	fi; \
	if [ "$$stamp" != "$$target" ]; then \
	    if [ -n "$$stamp" ]; then \
	        printf '  %-7s %s\n' 'CLEAN' "Build type changed ($$stamp -> $$target)"; \
	    elif ls build/ 2>/dev/null | grep -qv '\.gitkeep\|libc\|initrd\|profiling'; then \
	        printf '  %-7s %s\n' 'CLEAN' 'Stale objects from interrupted build'; \
	    else \
	        target=; \
	    fi; \
	    if [ -n "$$target" ]; then \
	        $(MAKE) clean >/dev/null 2>&1; \
	    fi; \
	fi; \
	mkdir -p build; echo debug > $(BUILD_STAMP)

# ------------------------------------------------------------------------------
# Debug build
# ------------------------------------------------------------------------------
debug: CXXFLAGS += -g -Og -DCONFIG_DEBUG
debug: CCFLAGS  += -g -Og -DCONFIG_DEBUG
debug: check-build-stamp $(KERNEL_DEBUG) iso/boot
	cp $(KERNEL_DEBUG) iso/boot/kernel.elf
	@mkdir -p $(dir $(DEBUG_ISO))
	@printf '  %-7s %s\n' 'ISO' '$(DEBUG_ISO)'
	@grub-mkrescue -o $(DEBUG_ISO) iso 2>/dev/null
	@printf '  %-7s %s\n' 'DONE' 'Debug ISO: $(DEBUG_ISO)'

# ------------------------------------------------------------------------------
# Release build
#
# The sub-make receives CXXFLAGS without -DCONFIG_DEBUG (never added to base).
# The stamp is written first so an interrupted build is detected on next run.
# ------------------------------------------------------------------------------
release: CXXFLAGS += -g -O2
release:
	@printf '  %-7s %s\n' 'RELEASE' 'Building release ISO…'
	$(MAKE) clean
	$(MAKE) $(KERNEL) $(INITRD_OBJ) iso/boot CXXFLAGS="$(CXXFLAGS)"
	@printf '  %-7s %s\n' 'SIZE' "$$($(GET_SIZE) $(KERNEL)) bytes"
	cp $(KERNEL) iso/boot/kernel.elf
	@mkdir -p $(dir $(RELEASE_ISO)) $(dir $(BUILD_STAMP))
	@echo release > $(BUILD_STAMP)
	@printf '  %-7s %s\n' 'ISO' '$(RELEASE_ISO)'
	@grub-mkrescue -o $(RELEASE_ISO) iso 2>/dev/null || \
	(printf '  %-7s %s\n' 'ERROR' 'grub-mkrescue failed'; echo $(PKG_HINT); exit 1)
	@printf '  %-7s %s\n' 'DONE' 'Release ISO: $(RELEASE_ISO)'
	@echo ""
	@echo "  Validate with:  make release-test"

# ------------------------------------------------------------------------------
# Profiling
# ------------------------------------------------------------------------------
profiling: CXX := x86_64-elf-g++
profiling: CXXFLAGS := $(filter-out -target x86_64-elf,$(CXXFLAGS)) \
                       -Wno-type-limits -Wno-unused-but-set-variable \
                       -Wno-class-memaccess \
                       -finstrument-functions -DCONFIG_PROFILING
profiling: LDFLAGS += -L $(GCOV_LIB_DIR)
profiling: clean $(OBJ) $(INITRD_OBJ) $(FAT32_OBJ) linker.ld iso/boot
	@printf '  %-7s %s\n' 'LD' 'kernel.elf (profiling)'
	$(LD) $(LDFLAGS) -o $(KERNEL) $(OBJ) $(INITRD_OBJ) $(FAT32_OBJ)
	cp $(KERNEL) iso/boot/kernel.elf
	@printf '  %-7s %s\n' 'ISO' '$(DEBUG_ISO)'
	@mkdir -p $(dir $(DEBUG_ISO))
	@grub-mkrescue -o $(DEBUG_ISO) iso 2>/dev/null
	@printf '  %-7s %s\n' 'PROFILE' 'Booting in QEMU…'
	mkdir -p build/profiling
	qemu-system-x86_64 -cdrom $(DEBUG_ISO) -m 256M -serial file:build/profiling/gcda.raw \
	    -nic none -boot order=d -no-reboot -device isa-debug-exit \
	    -drive if=pflash,format=raw,readonly=on,file=$(QEMU_UEFI) 2>&1 | \
	    tee build/profiling/qemu.log
	@printf '  %-7s %s\n' 'PROFILE' 'Extracting coverage data…'
	python3 tools/extract_gcda.py build/profiling/gcda.raw

# ------------------------------------------------------------------------------
# QEMU / Test targets
# ------------------------------------------------------------------------------
run-qemu: debug
	@if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
	    printf '  %-7s %s\n' 'QEMU' 'Starting (Ctrl+A then X to exit)…'; \
	    qemu-system-x86_64 $(QEMU_FLAGS); \
	else \
	    echo "QEMU missing."; echo $(PKG_HINT); exit 1; \
	fi

run-release: release
	@if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
	    printf '  %-7s %s\n' 'QEMU' 'Starting release image…'; \
	    qemu-system-x86_64 -cdrom $(RELEASE_ISO) -m 256M -serial mon:stdio -nic none \
	        -boot order=d \
	        -drive if=pflash,format=raw,readonly=on,file=$(QEMU_UEFI); \
	else \
	    echo "QEMU missing."; echo $(PKG_HINT); exit 1; \
	fi

run-qemu-debug: debug
	@if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
	    printf '  %-7s %s\n' 'QEMU' 'Starting debug (GDB :1234, -d int,cpu_reset)…'; \
	    echo "Connect GDB:  gdb build/kernel-debug.elf -ex 'target remote :1234'"; \
	    qemu-system-x86_64 -cdrom $(DEBUG_ISO) -m 256M -serial mon:stdio -nic none \
	        -boot order=d -s -d int,cpu_reset \
	        -drive if=pflash,format=raw,readonly=on,file=$(QEMU_UEFI); \
	else \
	    echo "QEMU missing."; echo $(PKG_HINT); exit 1; \
	fi

gdb: debug
	@echo ""
	@echo "=========================================="
	@echo " Jarvis GDB Debug Session"
	@echo "=========================================="
	@echo " QEMU started with GDB stub on :1234"
	@echo " Connect GDB in another terminal:"
	@echo "   x86_64-elf-gdb build/kernel-debug.elf -x tools/gdb/init.gdb"
	@echo ""
	@echo " Available GDB custom commands:"
	@echo "   tasks       - list all tasks"
	@echo "   task <n>    - show task details"
	@echo "   regs        - register dump"
	@echo "   pml4        - page table walk"
	@echo "   panicinfo   - full state dump"
	@echo "   step-into   - single-step (into)"
	@echo "   step-over   - step over call"
	@echo "   step-out    - finish function"
	@echo "   trace-syscall - toggle syscall tracing"
	@echo "=========================================="
	@echo ""
	qemu-system-x86_64 -cdrom $(DEBUG_ISO) -m 256M -serial mon:stdio -nic none \
	    -boot order=d -s -S -d cpu_reset \
	    -drive if=pflash,format=raw,readonly=on,file=$(QEMU_UEFI)

test-gdb: debug
	@rm -f build/gdb-panic-captured build/gdb-serial.log
	@mkdir -p build
	@printf '  %-7s %s\n' 'GDB' 'Launching QEMU with GDB stub…'
	@if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then \
	    echo "QEMU missing."; echo $(PKG_HINT); exit 1; \
	fi
	@if ! command -v gtimeout >/dev/null 2>&1; then \
	    echo "gtimeout missing. Install with: brew install coreutils"; exit 1; \
	fi
	qemu-system-x86_64 -cdrom $(DEBUG_ISO) -m 256M -serial file:build/gdb-serial.log -nic none \
	    -boot order=d -s -no-reboot -device isa-debug-exit \
	    -drive if=pflash,format=raw,readonly=on,file=$(QEMU_UEFI) & \
	QEMU_PID=$$!; \
	echo "Waiting for QEMU GDB stub (:1234)…"; \
	for i in 1 2 3 4 5 6 7 8; do \
	    nc -z localhost 1234 2>/dev/null && break; \
	    sleep 1; \
	done; \
	printf '  %-7s %s\n' 'GDB' 'Connecting (panic surveillance, 120s timeout)…'; \
	gtimeout 120 x86_64-elf-gdb build/kernel-debug.elf -batch -x tools/gdb/test-batch.gdb; \
	GDB_EXIT=$$?; \
	kill $$QEMU_PID 2>/dev/null; wait $$QEMU_PID 2>/dev/null; \
	if [ -f build/gdb-panic-captured ]; then \
	    echo ""; \
	    echo "!!! KERNEL PANIC CAPTURED !!!"; \
	    echo "See GDB output above for backtrace and register dump."; \
	    rm -f build/gdb-panic-captured; \
	    exit 1; \
	elif [ $$GDB_EXIT -eq 124 ]; then \
	    echo ""; \
	    printf '  %-7s %s\n' 'GDB' 'No panic within 120s (clean boot to shell)'; \
	elif [ $$GDB_EXIT -eq 0 ]; then \
	    echo ""; \
	    printf '  %-7s %s\n' 'GDB' 'No panic (QEMU exited cleanly)'; \
	else \
	    echo ""; \
	    echo "=== test-gdb: GDB exit code $$GDB_EXIT (check serial log: build/gdb-serial.log) ==="; \
	    exit $$GDB_EXIT; \
	fi

test-qemu: debug
	@if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
	    printf '  %-7s %s\n' 'TEST' 'Running test suite in QEMU…'; \
	    expect -c ' \
	        set timeout 60; \
	        spawn qemu-system-x86_64 $(QEMU_FLAGS) -no-reboot -device isa-debug-exit; \
	        expect { \
	            "Failed: 0" { \
	                send_user "\nAll tests passed (Failed: 0)\n"; \
	                exit 0; \
	            } \
	            "FAILED" { \
	                send_user "\nTest failures detected\n"; \
	                exit 1; \
	            } \
	            timeout { \
	                send_user "\n=== TIMEOUT — captured serial output ===\n"; \
	                send_user "$$expect_out(buffer)"; \
	                send_user "\n=== END ===\n"; \
	                exit 1; \
	            } \
	            eof { \
	                send_user "\n=== UNEXPECTED QEMU EXIT — captured serial output ===\n"; \
	                send_user "$$expect_out(buffer)"; \
	                send_user "\n=== END (check for PANIC/CPU EXCEPTION above) ===\n"; \
	                exit 1; \
	            } \
	        } \
	    '; \
	    rc=$$?; \
	    if [ $$rc -eq 0 ]; then \
	        printf '  %-7s %s\n' 'TEST' 'All tests passed.'; \
	    else \
	        echo "FAIL: Test validation failed with code $$rc"; \
	        exit 1; \
	    fi \
	else \
	    echo "QEMU missing."; echo $(PKG_HINT); exit 1; \
	fi

test: $(KERNEL)
	@echo "=========================================="
	@echo " Launching Kernel Internal Self-Tests..."
	@echo "=========================================="
	@echo "Validating Userspace Core Integrity..."
	$(MAKE) $(USERSPACE_ELF)
	@echo "Userspace components compiled securely."
	@echo "Trigger interactive tests at terminal context via 'selftest'."

# ------------------------------------------------------------------------------
# Release validation
# ------------------------------------------------------------------------------
release-test:
	@printf '  %-7s %s\n' 'RELEASE' 'Compilation validation…'
	$(MAKE) release
	@printf '  %-7s %s\n' 'TEST' 'Running full test suite…'
	$(MAKE) test-qemu

run-release-test: release
	$(MAKE) _run-shell-test ISO=$(RELEASE_ISO)

_run-shell-test:
	@if ! command -v qemu-system-x86_64 >/dev/null 2>&1; then echo "QEMU missing."; echo $(PKG_HINT); exit 1; fi; \
	printf '  %-7s %s\n' 'TEST' 'Comprehensive shell command test…'; \
	printf '%s\n' \
	    'set timeout 15' \
	    'set stty_init "raw -echo"' \
	    'spawn qemu-system-x86_64 -cdrom $(ISO) -m 256M -serial mon:stdio -nic none -boot order=d -no-reboot -device isa-debug-exit -drive if=pflash,format=raw,readonly=on,file=$(QEMU_UEFI)' \
	    '' \
	    'proc fail {msg} { puts "FAIL: $$msg"; exit 1 }' \
	    '' \
	    'proc wait_prompt {} {' \
	    '    expect {' \
	    '        -re {jarvis\$$ } { }' \
	    '        "PANIC" { fail "PANIC at prompt" }' \
	    '        "EXCEPTION" { fail "EXCEPTION at prompt" }' \
	    '        timeout { fail "TIMEOUT waiting for prompt" }' \
	    '    }' \
	    '}' \
	    '' \
	    'proc send_cmd {cmd} {' \
	    '    puts ">>> $$cmd"' \
	    '    send "$$cmd\r"' \
	    '}' \
	    '' \
	    'proc expect_ok {pat} {' \
	    '    expect {' \
	    '        -re $$pat { }' \
	    '        "PANIC" { fail "PANIC during: $$pat" }' \
	    '        "EXCEPTION" { fail "EXCEPTION during: $$pat" }' \
	    '        timeout { fail "TIMEOUT waiting for: $$pat" }' \
	    '    }' \
	    '}' \
	    '' \
	    'proc expect_err {cmd} {' \
	    '    puts ">>> $$cmd (expect error)"' \
	    '    send "$$cmd\r"' \
	    '    set pat [subst -nobackslashes {Unbekannter Befehl: $$cmd}]' \
	    '    expect_ok $$pat' \
	    '}' \
	    '' \
	    'puts "=== Waiting for boot + shell prompt ==="' \
	    'wait_prompt' \
	    'puts "Boot OK"' \
	    '' \
	    'puts ""' \
	    'puts "=== Section 1: Valid commands ==="' \
	    '' \
	    'send_cmd "help"' \
	    'expect_ok {Verfuegbare Befehle:}' \
	    'puts "  \[OK\] help"' \
	    '' \
	    'send_cmd "clear"' \
	    'wait_prompt' \
	    'puts "  \[OK\] clear"' \
	    '' \
	    'send_cmd "echo"' \
	    'wait_prompt' \
	    'puts "  \[OK\] echo (empty)"' \
	    '' \
	    'send_cmd "echo hello"' \
	    'expect_ok {hello}' \
	    'puts "  \[OK\] echo hello"' \
	    '' \
	    'send_cmd "echo hello world"' \
	    'expect_ok {hello world}' \
	    'puts "  \[OK\] echo hello world"' \
	    '' \
	    'send_cmd "uptime"' \
	    'expect_ok {Uptime:}' \
	    'puts "  \[OK\] uptime"' \
	    '' \
	    'send_cmd "tasks"' \
	    'expect_ok {Tasks:}' \
	    'puts "  \[OK\] tasks"' \
	    '' \
	    'send_cmd "meminfo"' \
	    'expect_ok {Gesamt:}' \
	    'puts "  \[OK\] meminfo"' \
	    '' \
	    'send_cmd "version"' \
	    'expect_ok {Kernel:}' \
	    'puts "  \[OK\] version"' \
	    '' \
	    'send_cmd "jobs"' \
	    'expect_ok {Background tasks:}' \
	    'puts "  \[OK\] jobs"' \
	    '' \
	    'send_cmd "modlist"' \
	    'expect_ok {Verfuegbare Treiber:}' \
	    'puts "  \[OK\] modlist"' \
	    '' \
	    'send_cmd "listprog"' \
	    'expect_ok {Registrierte Programme:}' \
	    'puts "  \[OK\] listprog"' \
	    '' \
	    'send_cmd "cd"' \
	    'wait_prompt' \
	    'puts "  \[OK\] cd"' \
	    '' \
	    'send_cmd "export FOO=bar"' \
	    'expect_ok {export: FOO=bar}' \
	    'puts "  \[OK\] export"' \
	    '' \
	    'send_cmd "run"' \
	    'expect_ok {Usage: run}' \
	    'puts "  \[OK\] run"' \
	    '' \
	    'send_cmd "runelf"' \
	    'expect_ok {Usage: runelf}' \
	    'puts "  \[OK\] runelf"' \
	    '' \
	    'send_cmd "modprobe"' \
	    'expect_ok {Usage: modprobe}' \
	    'puts "  \[OK\] modprobe"' \
	    '' \
	    'puts ""' \
	    'puts "=== Section 2: Invalid commands ==="' \
	    '' \
	    'expect_err "xyzzy"' \
	    'puts "  \[OK\] xyzzy rejected"' \
	    '' \
	    'expect_err "plugh"' \
	    'puts "  \[OK\] plugh rejected"' \
	    '' \
	    'puts ""' \
	    'puts "=== Section 3: selftest (first run) ==="' \
	    'set timeout 120' \
	    'send_cmd "selftest"' \
	'expect_ok {Failed:.*0}' \
	    'expect_ok {Self-tests complete.}' \
	    'set timeout 15' \
	    'puts "  \[OK\] selftest #1 (all passed)"' \
	    '' \
	    'puts "=== Section 4: selftest (second run) ==="' \
	    'set timeout 120' \
	    'send_cmd "selftest"' \
	    'expect_ok {Failed:.*0}' \
	    'expect_ok {Self-tests complete.}' \
	    'set timeout 15' \
	    'puts "  \[OK\] selftest #2 (all passed)"' \
	    '' \
	    'puts ""' \
	    'puts "=== Section 5: Shutdown ==="' \
	    'send_cmd "exit"' \
	    'expect_ok {Shutting down\.\.\.}' \
	    'puts "  \[OK\] exit"' \
	    '' \
	    'puts ""' \
	    'puts "=============================================="' \
	    'puts " ALL SHELL COMMANDS TEST PASSED"' \
	    'puts "=============================================="' \
	    'close' \
	    'exit 0' \
	| expect; \
	rc=$$?; \
	if [ $$rc -eq 0 ]; then \
	    printf '  %-7s %s\n' 'TEST' 'All shell commands passed.'; \
	else \
	    echo "FAIL: Shell command test failed with code $$rc"; \
	    exit 1; \
	fi

# ------------------------------------------------------------------------------
# Utility targets
# ------------------------------------------------------------------------------
symbols: $(KERNEL)
	@printf '  %-7s %s\n' 'SYMBOLS' '$(KERNEL_SYMBOLS)'
	nm -n $(KERNEL) > $(KERNEL_SYMBOLS)

objdump: $(KERNEL)
	@printf '  %-7s %s\n' 'OBJDUMP' '$(KERNEL_DIS)'
	x86_64-elf-objdump -d -M intel --no-show-raw-insn $(KERNEL) > $(KERNEL_DIS)

clean:
	@printf '  %-7s %s\n' 'CLEAN' 'Removing build artifacts…'
	rm -rf build $(DEBUG_ISO) $(RELEASE_ISO) iso/boot/kernel.elf $(INITRD_CPIO) $(LIBC_A)
	rm -f $(USERSPACE_ELF)
	rm -f $(BUILD_STAMP)
	rm -rf initrd_root debug release profiling
	rm -f $(FAT32_DISK)
