# Jarvis RTOS — Development Roadmap / Kernel Core
# Copyright (C) 2026 Arnold Hasshold
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

# ==============================================================================
# Jarvis RTOS Microkernel Makefile
# Target architecture: set ARCH to build for a different arch (default: x86_64)
# ==============================================================================

# ------------------------------------------------------------------------------
# Architecture selection — must be set before toolchain and source discovery
# ------------------------------------------------------------------------------
ARCH ?= x86_64

SUPPORTED_ARCHS := x86_64 aarch64 riscv64
ifneq ($(filter $(ARCH),$(SUPPORTED_ARCHS)),$(ARCH))
$(error Unsupported architecture '$(ARCH)'. Supported: $(SUPPORTED_ARCHS))
endif

# Architecture stamp — forces rebuild of arch-specific binaries (initrd, fat32)
# when ARCH changes between invocations.
# ------------------------------------------------------------------------------

# Derive upper-case ARCH for CONFIG_ARCH_* define
ARCH_UPPER := $(shell echo $(ARCH) | tr a-z A-Z)

QEMU_UEFI  ?= /opt/homebrew/share/qemu/edk2-$(ARCH)-code.fd

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
# No targets yet — targets are defined below.
# 
# Host-OS detection (Darwin/Linux) and selection of the correct
# cross-compiler triplet so bare-metal GCC/ld/objcopy are found
# correctly on both macOS (Homebrew) and Linux (distro packages).
#
# CXXFLAGS / CCFLAGS here are the *base* flags.  Build-type-specific
# flags (e.g. -Og -DCONFIG_DEBUG for debug, -O2 for release) are
# added via target-specific variable overrides on the 'debug'/'release'
# targets.
# ------------------------------------------------------------------------------

# --- Common CXXFLAGS (architecture-independent) ---
CXXFLAGS_COMMON := -std=c++20 -ffreestanding -fno-exceptions -fno-rtti \
                   -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
                   -fno-threadsafe-statics \
                   -Wall -Wextra -Werror \
                   -I src -I src/lib -pipe -MMD -MP \
                   -DCONFIG_ARCH_$(ARCH_UPPER) \
                   -ffunction-sections -fdata-sections

# 1. Cross-Compiler-Prefixe basierend auf dem Host-OS
ifeq ($(UNAME_S),Darwin)
    X86_64_TRIPLET  ?= x86_64-elf-
    AARCH64_TRIPLET ?= aarch64-elf-
    RISCV64_TRIPLET ?= riscv64-elf-
    export PATH := /opt/homebrew/bin:$(PATH)
else
    X86_64_TRIPLET  ?= x86_64-linux-gnu-
    AARCH64_TRIPLET ?= aarch64-linux-gnu-
    RISCV64_TRIPLET ?= riscv64-linux-gnu-
endif

# 2. Architektur-spezifische Werkzeuge und Flags
ifeq ($(ARCH),x86_64)

    CC          := $(X86_64_TRIPLET)gcc
    CXX         := $(X86_64_TRIPLET)g++
    LD          := $(X86_64_TRIPLET)ld
    AR          := $(X86_64_TRIPLET)ar
    OBJCOPY     := $(X86_64_TRIPLET)objcopy

    CXXFLAGS    := $(CXXFLAGS_COMMON) -m64 -mno-red-zone -mgeneral-regs-only -mcmodel=large
    CCFLAGS     := -m64 -static -nostdlib -ffreestanding -O2 -pipe -MMD -MP \
                   -ffunction-sections -fdata-sections

    AS          := nasm
    ASFLAGS     := -f elf64

    OBJCOPY_FMT  := elf64-x86-64
    OBJCOPY_ARCH := i386
    LDFLAGS     := -m elf_x86_64 -nostdlib -no-pie -T linker_$(ARCH).ld \
                   -z max-page-size=0x1000 -Map=build/kernel.map

    GCOV_LIB_DIR    := $(dir $(shell $(CC) -print-file-name=libgcov.a 2>/dev/null))
    QEMU_SYSTEM     := qemu-system-x86_64
    QEMU_ARCH_FLAGS := -boot order=d \
                       -drive if=pflash,format=raw,readonly=on,file=$(QEMU_UEFI)
    QEMU_DEBUG_EXIT := -device isa-debug-exit
    OBJDUMP_DIS_FLAGS := -M intel

else ifeq ($(ARCH),aarch64)

    CC          := $(AARCH64_TRIPLET)gcc
    CXX         := $(AARCH64_TRIPLET)g++
    LD          := $(AARCH64_TRIPLET)ld
    AR          := $(AARCH64_TRIPLET)ar
    OBJCOPY     := $(AARCH64_TRIPLET)objcopy

    CXXFLAGS    := $(CXXFLAGS_COMMON) -march=armv8-a
    CCFLAGS     := -static -nostdlib -ffreestanding -O2 -pipe -MMD -MP \
                   -ffunction-sections -fdata-sections \
                   -I src -I src/lib

    AS          := $(AARCH64_TRIPLET)as
    ASFLAGS     :=

    OBJCOPY_FMT  := elf64-littleaarch64
    OBJCOPY_ARCH := aarch64
    LDFLAGS      := -nostdlib -T linker_$(ARCH).ld -Map=build/kernel.map

    QEMU_SYSTEM     := qemu-system-aarch64
    QEMU_ARCH_FLAGS := -machine virt -cpu cortex-a72 -kernel $(KERNEL_DEBUG)
    QEMU_DEBUG_EXIT  :=
    OBJDUMP_DIS_FLAGS :=

else ifeq ($(ARCH),riscv64)

    CC          := $(RISCV64_TRIPLET)gcc
    CXX         := $(RISCV64_TRIPLET)g++
    LD          := $(RISCV64_TRIPLET)ld
    AR          := $(RISCV64_TRIPLET)ar
    OBJCOPY     := $(RISCV64_TRIPLET)objcopy

    CXXFLAGS    := $(CXXFLAGS_COMMON) -march=rv64imafdc -mabi=lp64d -mcmodel=medany
    CCFLAGS     := -static -nostdlib -ffreestanding -O2 -pipe -MMD -MP \
                   -ffunction-sections -fdata-sections \
                   -DCONFIG_ARCH_RISCV64 \
                   -mcmodel=medany

    AS          := $(RISCV64_TRIPLET)as
    ASFLAGS     :=

    OBJCOPY_FMT  := elf64-littleriscv
    OBJCOPY_ARCH := riscv
    LDFLAGS      := -nostdlib -T linker_$(ARCH).ld -Map=build/kernel.map
    LD_LIBS      :=

    QEMU_SYSTEM     := qemu-system-riscv64
    QEMU_ARCH_FLAGS := -machine virt -bios default
    OBJDUMP_DIS_FLAGS :=

endif

# ----- ccache (optional, no-op if not installed) -----
CCACHE := $(shell which ccache 2>/dev/null)
CXX := $(CCACHE) $(CXX)

# --- Abgeleitete Toolchain-Aliase ---
OBJDUMP := $(patsubst %-objcopy,%-objdump,$(OBJCOPY))
GDB     := $(patsubst %-objcopy,%-gdb,$(OBJCOPY))

# ------------------------------------------------------------------------------
# Output paths
# ------------------------------------------------------------------------------
KERNEL         := build/kernel.elf
KERNEL_DEBUG   := build/kernel-debug.elf
DEBUG_ISO      := debug/jarvis-rtos.iso
RELEASE_ISO    := release/jarvis-rtos.iso
KERNEL_SYMBOLS := build/kernel.sym
KERNEL_DIS     := build/kernel.dis

FAT32_DISK     := build/fat32.img
QEMU_NET       := -netdev user,id=net0 -device virtio-net-pci,netdev=net0,disable-legacy=on
QEMU_FLAGS     := -cdrom $(DEBUG_ISO) -m 256M -serial mon:stdio $(QEMU_NET) \
                  $(QEMU_ARCH_FLAGS)

# For x86_64 add IDE drive for FAT32
ifeq ($(ARCH),x86_64)
QEMU_FLAGS     += -drive file=$(FAT32_DISK),format=raw,if=ide,index=1,media=disk
endif

# For aarch64, load kernel directly instead of via ISO/GRUB
ifeq ($(ARCH),aarch64)
QEMU_FLAGS     := -kernel $(KERNEL_DEBUG) -m 256M -serial mon:stdio $(QEMU_NET) \
                  -machine virt -cpu cortex-a72 -display none -no-reboot \
                  -semihosting-config enable=on,target=native
endif

# For riscv64, QEMU 11+ has a fw_dynamic bug where ELF entry point is not
# passed to OpenSBI. Use raw binary instead (objcopy strips ELF headers).
KERNEL_RISCV_BIN := build/kernel-riscv64.bin

ifeq ($(ARCH),riscv64)
$(KERNEL_RISCV_BIN): $(KERNEL_DEBUG)
	$(OBJCOPY) -O binary $< $@

QEMU_FLAGS     := -kernel $(KERNEL_RISCV_BIN) -m 256M -serial mon:stdio \
	          $(QEMU_NET) -machine virt -bios default -display none -no-reboot
endif

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
        test-selftest test-all-debug test-all-release test-class \
        test-qemu test symbols objdump debug release release-test \
        run-release-test profiling gdb test-gdb rr-record rr-replay \
        _run-shell-test run-renode renode-test check-config config-summary

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

check-config:
	@printf '  %-7s %s\n' 'CONFIG' 'Validating kernel configuration…'
	@python3 tools/check-config.py; \
	rc=$$?; \
	if [ $$rc -eq 0 ]; then \
	    printf '  %-7s %s\n' 'CONFIG' 'All checks passed.'; \
	else \
	    printf '  %-7s %s\n' 'CONFIG' 'Errors found!'; \
	    exit 1; \
	fi

config-summary:
	@printf '  %-7s %s\n' 'CONFIG' 'Active configuration:'
	@grep -h '#define CONFIG_' src/kernel/jarvis_config.h | grep -v '//' | \
	sed 's/#define /  /' | column -t

help:
	@echo "Jarvis RTOS — Build Targets"
	@echo ""
	@echo "  [A] make (or make all)     Show this help"
	@echo "  [A] make build             Style check + debug ISO"
	@echo "  [A] make check-style       Validate kernel coding style (src/kernel/)"
	@echo "  [A] make check-config      Validate kernel configuration consistency"
	@echo "  [A] make config-summary    Print active configuration values"
	@echo "  [A] make debug             Build debug ISO"
	@echo "  [A] make release           Build production ISO"
	@echo "  [A] make clean             Remove all build artifacts"
	@echo "  [A] make test-selftest     Debug build, safe class, auto-shutdown (CI gate)"
	@echo "  [A] make test-all-debug    Debug build, all classes, auto-shutdown"
	@echo "  [A] make test-all-release  Release build, all classes, auto-shutdown"
	@echo "  [A] make test-class CLASS=<n>  Debug build, specific test class"
	@echo "  [A] make test-qemu         Legacy alias for test-all-debug"
	@echo "  [A] make release-test      Legacy alias for test-all-release"
	@echo "  [A] make run-release-test  All shell commands + 2x selftest + exit (release ISO)"
	@echo "  [A] make symbols           Kernel symbol table"
	@echo "  [A] make objdump           Kernel disassembly"
	@echo "  [A] make profiling         GCOV coverage capture"
	@echo "  [I] make run-qemu          Boot debug ISO (interactive shell)"
	@echo "  [I] make run-release       Build + boot release ISO (interactive shell)"
	@echo "  [I] make gdb               Debug ISO + GDB stub (:1234)"
	@echo "  [I] make test-gdb          Run tests under GDB surveillance"
	@echo "  [I] make rr-record         Record QEMU execution (deterministic replay)"
	@echo "  [I] make rr-replay         Replay recorded session with GDB stub (:1234)"
	@echo "  [I] make run-renode        Boot debug ISO in Renode (x86_64)"
	@echo "  [I] make run-renode RENODE_ARCH=aarch64  Boot AArch64 in Renode"
	@echo "  [I] make run-renode RENODE_ARCH=riscv64  Boot RISC-V 64 in Renode"
	@echo "  [A] make renode-test       Run selftest in Renode (x86_64)"
	@echo ""
	@echo "  [A] = autonomous (run and done, no interaction needed)"
	@echo "  [I] = interactive (user sits at the QEMU/GDB/Renode console)"

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
debug: check-build-stamp $(KERNEL_DEBUG)
ifeq ($(ARCH),x86_64)
	cp $(KERNEL_DEBUG) iso/boot/kernel.elf
	@mkdir -p $(dir $(DEBUG_ISO))
	@printf '  %-7s %s\n' 'ISO' '$(DEBUG_ISO)'
	@grub-mkrescue -o $(DEBUG_ISO) iso 2>&1 || \
	 (printf '  %-7s %s\n' 'ERROR' 'grub-mkrescue failed'; echo 'Install grub-pc + xorriso'; exit 1)
	@printf '  %-7s %s\n' 'DONE' 'Debug ISO: $(DEBUG_ISO)'
else ifeq ($(ARCH),riscv64)
	$(OBJCOPY) -O binary $(KERNEL_DEBUG) $(KERNEL_RISCV_BIN)
	@printf '  %-7s %s\n' 'BIN' '$(KERNEL_RISCV_BIN)'
	@printf '  %-7s %s\n' 'DONE' 'Debug kernel: $(KERNEL_DEBUG)'
else
	@printf '  %-7s %s\n' 'DONE' 'Debug kernel: $(KERNEL_DEBUG)'
endif

# ------------------------------------------------------------------------------
# Release build
#
# The sub-make receives CXXFLAGS without -DCONFIG_DEBUG (never added to base).
# The stamp is written first so an interrupted build is detected on next run.
# ------------------------------------------------------------------------------
release: CXXFLAGS += -g -O2
release:
ifneq ($(ARCH),x86_64)
	@printf '  %-7s %s\n' 'ERROR' 'Release builds only supported on x86_64 (arch=$(ARCH))'
	@exit 1
endif
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
ifneq ($(ARCH),x86_64)
profiling:
	@printf '  %-7s %s\n' 'ERROR' 'Profiling only supported on x86_64 (arch=$(ARCH))'; exit 1
else
profiling: CXX := x86_64-elf-g++
profiling: CXXFLAGS := $(filter-out -target x86_64-elf,$(CXXFLAGS)) \
                       -Wno-type-limits -Wno-unused-but-set-variable \
                       -Wno-class-memaccess \
                       -finstrument-functions -DCONFIG_PROFILING
profiling: LDFLAGS += -L $(GCOV_LIB_DIR)
profiling: clean $(OBJ) $(INITRD_OBJ) $(FAT32_OBJ) linker_$(ARCH).ld iso/boot
	@printf '  %-7s %s\n' 'LD' 'kernel.elf (profiling)'
	$(LD) $(LDFLAGS) -o $(KERNEL) $(OBJ) $(INITRD_OBJ) $(FAT32_OBJ)
	cp $(KERNEL) iso/boot/kernel.elf
	@printf '  %-7s %s\n' 'ISO' '$(DEBUG_ISO)'
	@mkdir -p $(dir $(DEBUG_ISO))
	@grub-mkrescue -o $(DEBUG_ISO) iso 2>/dev/null
	@printf '  %-7s %s\n' 'PROFILE' 'Booting in QEMU…'
	mkdir -p build/profiling
	$(QEMU_SYSTEM) -cdrom $(DEBUG_ISO) -m 256M -serial file:build/profiling/gcda.raw \
	    $(QEMU_NET) -boot order=d -no-reboot -device isa-debug-exit \
	    -drive if=pflash,format=raw,readonly=on,file=$(QEMU_UEFI) 2>&1 | \
	    tee build/profiling/qemu.log
	@printf '  %-7s %s\n' 'PROFILE' 'Extracting coverage data…'
	python3 tools/extract_gcda.py build/profiling/gcda.raw
endif

# ------------------------------------------------------------------------------
# QEMU / Test targets
# ------------------------------------------------------------------------------
run-qemu: debug
	@if command -v $(QEMU_SYSTEM) >/dev/null 2>&1; then \
	    printf '  %-7s %s\n' 'QEMU' 'Starting (Ctrl+A then X to exit)…'; \
	    $(QEMU_SYSTEM) $(QEMU_FLAGS); \
	else \
	    echo "QEMU missing."; echo $(PKG_HINT); exit 1; \
	fi

run-release: release
ifneq ($(ARCH),x86_64)
	@printf '  %-7s %s\n' 'ERROR' 'Run release only supported on x86_64 (arch=$(ARCH))'; exit 1
endif
	@if command -v $(QEMU_SYSTEM) >/dev/null 2>&1; then \
	    printf '  %-7s %s\n' 'QEMU' 'Starting release image…'; \
	    $(QEMU_SYSTEM) -cdrom $(RELEASE_ISO) -m 256M -serial mon:stdio $(QEMU_NET) \
	        -boot order=d \
	        -drive if=pflash,format=raw,readonly=on,file=$(QEMU_UEFI) \
	        -drive file=$(FAT32_DISK),format=raw,if=ide,index=1,media=disk; \
	else \
	    echo "QEMU missing."; echo $(PKG_HINT); exit 1; \
	fi

run-qemu-debug: debug
	@if command -v $(QEMU_SYSTEM) >/dev/null 2>&1; then \
	    printf '  %-7s %s\n' 'QEMU' 'Starting debug (GDB :1234, -d int,cpu_reset)…'; \
	    echo "Connect GDB:  gdb build/kernel-debug.elf -ex 'target remote :1234'"; \
	    $(QEMU_SYSTEM) $(QEMU_FLAGS) -s -d int,cpu_reset; \
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
	@echo "   $(GDB) build/kernel-debug.elf -x tools/gdb/init.gdb"
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
	$(QEMU_SYSTEM) $(QEMU_FLAGS) -s -S -d cpu_reset

test-gdb: debug
ifneq ($(ARCH),x86_64)
	@printf '  %-7s %s\n' 'ERROR' 'test-gdb only supported on x86_64 (arch=$(ARCH))'; exit 1
endif
	@rm -f build/gdb-panic-captured build/gdb-serial.log
	@mkdir -p build
	@printf '  %-7s %s\n' 'GDB' 'Launching QEMU with GDB stub…'
	@if ! command -v $(QEMU_SYSTEM) >/dev/null 2>&1; then \
	    echo "QEMU missing."; echo $(PKG_HINT); exit 1; \
	fi
	@if ! command -v gtimeout >/dev/null 2>&1; then \
	    echo "gtimeout missing. Install with: brew install coreutils"; exit 1; \
	fi
	$(QEMU_SYSTEM) -cdrom $(DEBUG_ISO) -m 256M -serial file:build/gdb-serial.log $(QEMU_NET) \
	    -boot order=d -s -no-reboot -device isa-debug-exit \
	    -drive if=pflash,format=raw,readonly=on,file=$(QEMU_UEFI) & \
	QEMU_PID=$$!; \
	echo "Waiting for QEMU GDB stub (:1234)…"; \
	for i in 1 2 3 4 5 6 7 8; do \
	    nc -z localhost 1234 2>/dev/null && break; \
	    sleep 1; \
	done; \
	printf '  %-7s %s\n' 'GDB' 'Connecting (panic surveillance, 120s timeout)…'; \
	gtimeout 120 $(GDB) build/kernel-debug.elf -batch -x tools/gdb/test-batch.gdb; \
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

# ------------------------------------------------------------------------------
# Deterministic replay (QEMU -icount + record/replay for race debugging)
#
# Usage:
#   make rr-record    — boots the debug ISO and records execution to build/rr.log
#   make rr-replay    — replays the recorded session with GDB stub on :1234
#
# During replay, connect GDB and set breakpoints before the race window:
#   $(GDB) build/kernel-debug.elf -ex 'target remote :1234'
# The deterministic -icount ensures the same instruction sequence every replay.
# ------------------------------------------------------------------------------
rr-record: debug
	@printf '  %-7s %s\n' 'RR' 'Recording deterministic trace…'
	@mkdir -p build
	$(QEMU_SYSTEM) $(QEMU_FLAGS) -icount shift=1,sleep=off -record build/rr.log -display none

rr-replay:
	@if [ ! -f build/rr.log ]; then \
	    echo "No recording found — run 'make rr-record' first."; exit 1; \
	fi
	@printf '  %-7s %s\n' 'RR' 'Replaying with GDB stub on :1234…'
	@echo "Connect GDB:  $(GDB) build/kernel-debug.elf -ex 'target remote :1234'"
	$(QEMU_SYSTEM) $(QEMU_FLAGS) -icount shift=1,sleep=off -replay build/rr.log -s -S -display none

# ------------------------------------------------------------------------------
# Renode targets
# ------------------------------------------------------------------------------
RENODE_ARCH ?= x86_64
RENODE_SUPPORTED_ARCHS := x86_64 aarch64 riscv64

run-renode: debug
	@if ! command -v renode >/dev/null 2>&1; then \
	    echo "Renode missing. Install with: brew install renode/tap/renode"; exit 1; \
	fi
	@if [ "$(filter $(RENODE_ARCH),$(RENODE_SUPPORTED_ARCHS))" != "$(RENODE_ARCH)" ]; then \
	    echo "Unsupported Renode arch '$(RENODE_ARCH)'. Supported: $(RENODE_SUPPORTED_ARCHS)"; exit 1; \
	fi
	@printf '  %-7s %s\n' 'RENODE' 'Starting Jarvis RTOS ($(RENODE_ARCH))…'
	@printf '  %-7s %s\n' 'SCRIPT' 'tools/renode/jarvis-$(RENODE_ARCH).resc'
	renode --disable-xwt -e "i @tools/renode/jarvis-$(RENODE_ARCH).resc; s"

renode-test: debug
	@if ! command -v renode >/dev/null 2>&1; then \
	    echo "Renode missing. Install with: brew install renode/tap/renode"; exit 1; \
	fi
	@if [ "$(filter $(RENODE_ARCH),$(RENODE_SUPPORTED_ARCHS))" != "$(RENODE_ARCH)" ]; then \
	    echo "Unsupported Renode arch '$(RENODE_ARCH)'. Supported: $(RENODE_SUPPORTED_ARCHS)"; exit 1; \
	fi
	@printf '  %-7s %s\n' 'RENODE' 'Running selftest (safe class) on $(RENODE_ARCH)…'
	@mkdir -p initrd/tests
	@printf 'safe\n' > initrd/tests/test-config.txt
	$(MAKE) debug
	renode --disable-xwt --console -e "i @tools/renode/jarvis-$(RENODE_ARCH).resc; start;"

# ------------------------------------------------------------------------------
# Test targets
# ------------------------------------------------------------------------------

# Shared expect helper for autonomous test runs.
# Single consistent serial log path for all automated test runs.
TEST_SERIAL_LOG := /tmp/jarvis-serial.log

# Host-side watchdog: if $(TEST_SERIAL_LOG) does not grow for WATCHDOG_STALL seconds,
# kill QEMU and append a diagnostic message.  This catches hangs that the
# in-kernel watchdog cannot detect (interrupts disabled during test execution).
WATCHDOG_STALL := 60

# Usage: $(call _run_test_qemu,<description>,<timeout_sec>)
define _run_test_qemu
	@if ! command -v $(QEMU_SYSTEM) >/dev/null 2>&1; then echo "QEMU missing."; echo $(PKG_HINT); exit 1; fi; \
	printf '  %-7s %s\n'  'TEST'   '$(1)…'; \
	rm -f "$(TEST_SERIAL_LOG)"; \
	\
	( \
	    trap "exit 0" TERM; \
	    LAST_SIZE=-1; \
	    STALL=0; \
	    while true; do \
	        sleep 1; \
	        CUR_SIZE=$$(wc -c < "$(TEST_SERIAL_LOG)" 2>/dev/null || echo 0); \
	        if [ "$$CUR_SIZE" = "$$LAST_SIZE" ]; then \
	            STALL=$$((STALL + 1)); \
	            if [ $$STALL -ge $(WATCHDOG_STALL) ]; then \
	                echo "[HOST-WATCHDOG] Tests interrupted — no serial output for $(WATCHDOG_STALL) s" >> "$(TEST_SERIAL_LOG)"; \
	                pkill -f "$(QEMU_SYSTEM).*jarvis-rtos" 2>/dev/null; \
	                exit 1; \
	            fi; \
	        else \
	            STALL=0; \
	            LAST_SIZE=$$CUR_SIZE; \
	        fi; \
	    done; \
	) & \
	MONITOR_PID=$$!; \
	\
	expect -c ' \
	    fconfigure stdout -buffering none; \
	    set timeout $(2); \
	    spawn $(QEMU_SYSTEM) $(QEMU_FLAGS) -display none -no-reboot $(QEMU_DEBUG_EXIT); \
	    expect { \
	        -re {\\r*\\n==============================\\r*\\n TEST SUMMARY\\r*\\n==============================\\r*\\n  PLANNED:\\s+(\\d+)\\r*\\n  EXECUTED:\\s+(\\d+)\\r*\\n  TIME_ELAPSED_MS:\\s+(\\d+)\\r*\\n(?:  BOOT_TIME_MS:\\s+(\\d+)\\r*\\n)?  PASSED:\\s+(\\d+)\\r*\\n  FAILED:\\s+(\\d+)\\r*\\n==============================} { \
	            puts "$$expect_out(buffer)"; flush stdout; \
	            set test_ms $$expect_out(3,string); \
	            set boot_ms $$expect_out(4,string); \
	            if {$$expect_out(1,string) == $$expect_out(2,string) && $$expect_out(6,string) == 0} { \
	                if {[string length $$boot_ms] > 0} { \
	                    puts "RESULT: PASS ($$expect_out(1,string) tests, test=$$test_ms ms, boot=$$boot_ms ms)"; \
	                } else { \
	                    puts "RESULT: PASS ($$expect_out(1,string) tests, test=$$test_ms ms)"; \
	                } \
	                flush stdout; \
	                catch {exec kill [exp_pid] 2>/dev/null}; \
	                exit 0; \
	            } else { \
	                puts "RESULT: FAIL (planned=$$expect_out(1,string) executed=$$expect_out(2,string) failed=$$expect_out(6,string))"; flush stdout; \
	                catch {exec kill [exp_pid] 2>/dev/null}; \
	                exit 1; \
	            } \
	        } \
	        timeout { \
	            puts "\\n=== TIMEOUT ===\\n"; puts "$$expect_out(buffer)"; puts "\\n=== END ===\\n"; flush stdout; exit 1; \
	        } \
	        eof { \
	            puts "\\n=== QEMU EXIT (no summary) ===\\n"; puts "$$expect_out(buffer)"; puts "\\n=== END ===\\n"; flush stdout; exit 1; \
	        } \
	    } \
	' 2>&1 | tee "$(TEST_SERIAL_LOG)"; \
	rc=$${PIPESTATUS[0]}; \
	\
	# Kill QEMU immediately after expect exits — shutdown_kernel() may not work \
	# (UEFI firmware can intercept port-based shutdown methods).  This avoids \
	# the 60-second host-watchdog wait for a dead QEMU process. \
	pkill -f "$(QEMU_SYSTEM).*jarvis-rtos" 2>/dev/null || true; \
	\
	kill $$MONITOR_PID 2>/dev/null; \
	wait $$MONITOR_PID 2>/dev/null; \
	\
	if [ $$rc -eq 0 ]; then \
	    :; \
	else \
	    printf '  %-7s %s\n' 'SERIAL' '$(TEST_SERIAL_LOG)'; \
	    echo "FAIL: Test validation failed with code $$rc"; \
	    exit 1; \
	fi
endef

test-selftest:
	@mkdir -p initrd/tests
	@printf 'safe\n' > initrd/tests/test-config.txt
	$(MAKE) debug
	$(call _run_test_qemu,Running selftest (safe class),120)

test-all-debug:
	@mkdir -p initrd/tests
	@printf 'all\n' > initrd/tests/test-config.txt
	$(MAKE) debug
	$(call _run_test_qemu,Running all debug tests,180)

test-all-release:
	@mkdir -p initrd/tests
	@printf 'all\n' > initrd/tests/test-config.txt
	$(MAKE) release
	$(call _run_test_qemu,Running all release tests,120)

test-class:
	@if [ -z "$(CLASS)" ]; then echo "Usage: make test-class CLASS=<name>"; exit 1; fi
	@mkdir -p initrd/tests
	@printf '%s\n' "$(CLASS)" > initrd/tests/test-config.txt
	$(MAKE) debug
	$(call _run_test_qemu,Running test class "$(CLASS)",120)

# Legacy aliases
test-qemu: test-all-debug

release-test: test-all-release

test: $(KERNEL)
	@echo "=========================================="
	@echo " Launching Kernel Internal Self-Tests..."
	@echo "=========================================="
	@echo "Validating Userspace Core Integrity..."
	$(MAKE) $(USERSPACE_ELF)
	@echo "Userspace components compiled securely."
	@echo "Trigger interactive tests at terminal context via 'selftest'."

run-release-test: release
	$(MAKE) _run-shell-test ISO=$(RELEASE_ISO)

_run-shell-test:
	@if [ "$(ARCH)" != "x86_64" ]; then printf '  %-7s %s\n' 'ERROR' 'Shell test only supported on x86_64 (arch=$(ARCH))'; exit 1; fi
	@if ! command -v $(QEMU_SYSTEM) >/dev/null 2>&1; then echo "QEMU missing."; echo $(PKG_HINT); exit 1; fi; \
	printf '  %-7s %s\n' 'TEST' 'Comprehensive shell command test…'; \
	printf '%s\n' \
	    'set timeout 15' \
	    'set stty_init "raw -echo"' \
	    'spawn $(QEMU_SYSTEM) -cdrom $(ISO) -m 256M -serial mon:stdio $(QEMU_NET) -boot order=d -no-reboot -device isa-debug-exit -drive if=pflash,format=raw,readonly=on,file=$(QEMU_UEFI)' \
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
	    '# NOTE: Section 5 (exit) MUST remain the last test in the sequence' \
	    '# because it shuts down the system and terminates QEMU.' \
	    '# Any tests added after this section will never execute.' \
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
	$(OBJDUMP) -d $(OBJDUMP_DIS_FLAGS) --no-show-raw-insn $(KERNEL) > $(KERNEL_DIS)

clean:
	@printf '  %-7s %s\n' 'CLEAN' 'Removing build artifacts…'
	rm -rf build $(DEBUG_ISO) $(RELEASE_ISO) iso/boot/kernel.elf $(INITRD_CPIO) $(LIBC_A)
	rm -f $(USERSPACE_ELF)
	rm -f $(BUILD_STAMP)
	rm -rf initrd_root debug release profiling
	rm -f $(FAT32_DISK)
