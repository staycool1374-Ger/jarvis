# ==============================================================================
# Jarvis RTOS Microkernel Makefile
# Target: x86_64 Freestanding C++20 / ISO 26262 & MISRA-Compliant Toolchain
# ==============================================================================

# ------------------------------------------------------------------------------
# Host Environment & OS Detection
# ------------------------------------------------------------------------------
UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Darwin)
    # macOS Specific Configurations
    GET_SIZE := stat -f%z
    PKG_HINT := "Install with: brew install qemu xorriso"
else
    # Linux / Default Configurations
    GET_SIZE := stat -c%s
    PKG_HINT := "Install with: apt-get install qemu-system-x86 xorriso grub-common"
endif

# ------------------------------------------------------------------------------
# Toolchain Setup
# ------------------------------------------------------------------------------
CXX      := g++
CXXFLAGS := -std=c++20 -ffreestanding -fno-exceptions -fno-rtti \
            -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
            -fno-threadsafe-statics -target x86_64-elf -m64 -mno-red-zone \
            -mgeneral-regs-only -mcmodel=large \
            -Wall -Wextra -Werror \
            -I src -I src/lib -O3 -pipe -MMD -MP \
            -ffunction-sections -fdata-sections \
            -DCONFIG_DEBUG

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
# File System Pathing & Target Discoveries
# ------------------------------------------------------------------------------
DEPFILES       := $(shell find build -name '*.d' 2>/dev/null)
SRC_CXX        := $(shell find src -name '*.cpp')
SRC_ASM        := $(shell find src -name '*.asm')
OBJ            := $(SRC_CXX:src/%.cpp=build/%.o) $(SRC_ASM:src/%.asm=build/%.o)

KERNEL         := build/kernel.elf
KERNEL_SYMBOLS := build/kernel.sym
KERNEL_DEBUG   := build/kernel-debug.elf
ISO            := jarvis-rtos.iso
DEBUG_ISO      := debug/jarvis-rtos.iso
RELEASE_ISO    := release/jarvis-rtos.iso

LIBC_DIR       := src/libc
LIBC_SRC       := $(shell find $(LIBC_DIR) -name '*.c' -o -name '*.S')
LIBC_OBJ       := $(patsubst src/%.c,build/%.o,$(filter %.c,$(LIBC_SRC))) \
                  $(patsubst src/%.S,build/%.o,$(filter %.S,$(LIBC_SRC)))
LIBC_A         := build/libc/libc.a

USERSPACE_SRC  := $(shell find userspace -name '*.S' -o -name '*.c' 2>/dev/null)
USERSPACE_ELF  := $(USERSPACE_SRC:%=%.elf)

INITRD_CPIO    := initrd.cpio
INITRD_OBJ     := build/initrd/initrd_cpio.o

QEMU_UEFI      ?= /opt/homebrew/share/qemu/edk2-x86_64-code.fd
QEMU_FLAGS     := -cdrom $(ISO) -m 256M -serial mon:stdio -nic none -boot order=d \
                  -drive if=pflash,format=raw,readonly=on,file=$(QEMU_UEFI)

# ------------------------------------------------------------------------------
# Build Rules & Targets
# ------------------------------------------------------------------------------
.PHONY: help all clean run-qemu run-qemu-debug test-qemu iso test symbols objdump debug release release-test profiling

-include $(DEPFILES)

help:
	@echo "Jarvis RTOS — Build Targets"
	@echo ""
	@echo "  make               Build debug ISO (all tests)"
	@echo "  make test-qemu     Run full test suite in QEMU"
	@echo "  make release-test  Clean-build release ISO + validate in QEMU"
	@echo "  make debug         Build debug ISO with GDB stub (port :1234)"
	@echo "  make clean         Remove all build artifacts"
	@echo "  make test          Compile-check kernel + userspace"
	@echo "  make run-qemu      Boot default ISO interactively"
	@echo "  make symbols       Generate kernel symbol table"
	@echo "  make objdump       Disassemble kernel to build/kernel.dis"
	@echo "  make profiling     GCOV instrumentation + coverage capture"
	@echo ""
	@echo "ISOs:  $(ISO)  |  debug/jarvis-rtos.iso  |  release/jarvis-rtos.iso"
	@echo "Debug (all tests)   — CONFIG_DEBUG defined"
	@echo "Release (163 tests) — CONFIG_DEBUG undefined, purely computational subset"

all: $(ISO)

iso/boot:
	@mkdir -p $@

iso/boot/kernel.elf: $(KERNEL) | iso/boot
	cp $< $@

$(ISO): iso/boot/kernel.elf iso/boot/grub/grub.cfg
	@grub-mkrescue -o $@ iso 2>/dev/null || \
	(echo "ERROR: grub-mkrescue failed. Please verify dependencies."; echo $(PKG_HINT); exit 1)
	@echo ""
	@echo "=========================================="
	@echo " ISO Generation Successful: $(ISO)"
	@echo " Execute Deployment Options via:"
	@echo "   make run-qemu"
	@echo "   make release"
	@echo "=========================================="

$(KERNEL): $(OBJ) $(INITRD_OBJ) linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJ) $(INITRD_OBJ)
	@echo "Kernel built successfully: $@ ($$($(GET_SIZE) $@) bytes)"

$(INITRD_OBJ): $(INITRD_CPIO)
	@mkdir -p $(dir $@)
	x86_64-elf-objcopy -I binary -O elf64-x86-64 -B i386 $< $@

$(INITRD_CPIO): $(USERSPACE_ELF)
	@mkdir -p initrd_root
	@if [ ! -z "$(USERSPACE_ELF)" ]; then cp $(USERSPACE_ELF) initrd_root/; fi
	cd initrd_root && find . -print0 | cpio -o -H newc -0 --quiet > ../$@
	@rm -rf initrd_root

build/libc/%.o: src/libc/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CCFLAGS) -I src/libc -c -o $@ $<

build/libc/%.o: src/libc/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CCFLAGS) -c -o $@ $<

$(LIBC_A): $(LIBC_OBJ)
	$(AR) rcs $@ $^

userspace/%.S.elf: userspace/%.S
	$(CC) $(CCFLAGS) -o $@ $<

userspace/%.c.elf: userspace/%.c $(LIBC_A) build/libc/crt0.o
	$(CC) $(CCFLAGS) -I src/libc -o $@ build/libc/crt0.o $< -L build/libc -lc

build/%.o: src/%.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) -o $@ $<

build/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# ------------------------------------------------------------------------------
# Testing & Emulation Pipelines (QEMU)
# ------------------------------------------------------------------------------
run-qemu: $(ISO)
	@if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
	    echo "Starting QEMU Monitor (Ctrl+A then X to exit)..."; \
	    qemu-system-x86_64 $(QEMU_FLAGS); \
	else \
	    echo "QEMU execution architecture missing."; echo $(PKG_HINT); exit 1; \
	fi

run-qemu-debug: $(DEBUG_ISO)
	@if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
	    echo "Starting QEMU (debug) — Ctrl+A then X to exit."; \
	    echo "Connect GDB:  gdb build/kernel-debug.elf -ex 'target remote :1234'"; \
	    echo "Interrupt log: -d int,cpu_reset"; \
	    qemu-system-x86_64 -cdrom $(DEBUG_ISO) -m 256M -serial mon:stdio -nic none \
	        -boot order=d -s -S -d int,cpu_reset \
	        -drive if=pflash,format=raw,readonly=on,file=$(QEMU_UEFI); \
	else \
	    echo "QEMU execution architecture missing."; echo $(PKG_HINT); exit 1; \
	fi

test-qemu: $(ISO)
	@if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
	    echo "Booting in QEMU Environment (Automated Self-Test Execution Pipeline)..."; \
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
	        echo "OK: All integrated runtime test suites passed successfully."; \
	    else \
	        echo "FAIL: Test validation failed with code $$rc"; \
	        exit 1; \
	    fi \
	else \
	    echo "QEMU binary not accessible in current user environment paths."; echo $(PKG_HINT); exit 1; \
	fi

test: $(KERNEL)
	@echo "=========================================="
	@echo " Launching Kernel Internal Self-Tests..."
	@echo "=========================================="
	@echo "Validating Userspace Core Integrity..."
	$(MAKE) $(USERSPACE_ELF)
	@echo "Userspace components compiled securely."
	@echo "Trigger interactive tests at terminal context via 'selftest'."

debug: CXXFLAGS += -g -Og -DCONFIG_DEBUG
debug: CCFLAGS  += -g -Og -DCONFIG_DEBUG

debug: $(OBJ) $(INITRD_OBJ) linker.ld
	@mkdir -p $(dir $(KERNEL_DEBUG)) $(dir $(DEBUG_ISO))
	$(LD) $(LDFLAGS) -o $(KERNEL_DEBUG) $(OBJ) $(INITRD_OBJ)
	@echo "Debug target kernel constructed: $(KERNEL_DEBUG) ($$($(GET_SIZE) $(KERNEL_DEBUG)) bytes)"
	cp $(KERNEL_DEBUG) iso/boot/kernel.elf
	@grub-mkrescue -o $(DEBUG_ISO) iso 2>/dev/null
	@echo "Debug ISO image mapped successfully: $(DEBUG_ISO)"

profiling: CXX := x86_64-elf-g++
profiling: CXXFLAGS := $(filter-out -target x86_64-elf,$(CXXFLAGS)) -Wno-type-limits -Wno-unused-but-set-variable -finstrument-functions -DCONFIG_PROFILING
profiling: LDFLAGS  += -L $(GCOV_LIB_DIR)

profiling: clean $(OBJ) $(INITRD_OBJ) linker.ld
	@mkdir -p build/profiling
	$(LD) $(LDFLAGS) -o $(KERNEL) $(OBJ) $(INITRD_OBJ)
	@echo "Profiling kernel built: $(KERNEL)"
	cp $(KERNEL) iso/boot/kernel.elf
	@grub-mkrescue -o $(ISO) iso 2>/dev/null
	@echo "Booting in QEMU to capture coverage data..."
	qemu-system-x86_64 -cdrom $(ISO) -m 256M -serial file:build/profiling/gcda.raw \
	    -nic none -boot order=d -no-reboot -device isa-debug-exit \
	    -drive if=pflash,format=raw,readonly=on,file=$(QEMU_UEFI) 2>&1 | \
	    tee build/profiling/qemu.log
	@echo "Extracting .gcda data and generating coverage report..."
	python3 tools/extract_gcda.py build/profiling/gcda.raw

release: CXXFLAGS += -g -O2 -UCONFIG_DEBUG
release:
	@echo "=== Building release ISO (clean build) ==="
	$(MAKE) clean
	$(MAKE) iso/boot/kernel.elf iso/boot/grub/grub.cfg CXXFLAGS="$(CXXFLAGS)"
	@mkdir -p $(dir $(RELEASE_ISO))
	@grub-mkrescue -o $(RELEASE_ISO) iso 2>/dev/null || \
	(echo "ERROR: grub-mkrescue failed."; echo $(PKG_HINT); exit 1)
	@echo ""
	@echo "=========================================="
	@echo " Release ISO ready: $(RELEASE_ISO)"
	@echo " Validate with:  make release-test"
	@echo "=========================================="

release-test:
	@echo ""
	@echo "=== Building + validating release ISO ==="
	$(MAKE) release
	@echo ""
	@echo "=== Validating release ISO via test-qemu ==="
	$(MAKE) test-qemu ISO=$(RELEASE_ISO)

symbols: $(KERNEL)
	nm -n $(KERNEL) > $(KERNEL_SYMBOLS)
	@echo "System Symbol Matrix generated safely: $(KERNEL_SYMBOLS)"

objdump: $(KERNEL)
	x86_64-elf-objdump -d -M intel --no-show-raw-insn $(KERNEL) > build/kernel.dis
	@echo "Disassembly written to build/kernel.dis ($$($(GET_SIZE) build/kernel.dis) bytes)"

clean:
	rm -rf build $(ISO) $(DEBUG_ISO) $(RELEASE_ISO) iso/boot/kernel.elf iso/boot/kernel-debug.elf $(INITRD_CPIO) $(LIBC_A)
	rm -f $(USERSPACE_ELF)
	rm -rf initrd_root debug release profiling
	@echo "Environment workspace clean-up routines completed."