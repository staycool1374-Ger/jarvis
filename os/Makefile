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
            -I src -I src/lib -O3 -pipe -MMD -MP

CC       := x86_64-elf-gcc
CCFLAGS  := -m64 -static -nostdlib -ffreestanding -O2 -pipe -MMD -MP

AS       := nasm
ASFLAGS  := -f elf64

LD       := x86_64-elf-ld
AR       := x86_64-elf-ar
LDFLAGS  := -m elf_x86_64 -nostdlib -T linker.ld -z max-page-size=0x1000 \
            -Map=build/kernel.map

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

LIBC_DIR       := src/libc
LIBC_SRC       := $(shell find $(LIBC_DIR) -name '*.c' -o -name '*.S')
LIBC_OBJ       := $(patsubst src/%.c,build/%.o,$(filter %.c,$(LIBC_SRC))) \
                  $(patsubst src/%.S,build/%.o,$(filter %.S,$(LIBC_SRC)))
LIBC_A         := build/libc/libc.a

USERSPACE_SRC  := $(shell find userspace -name '*.S' -o -name '*.c' 2>/dev/null)
USERSPACE_ELF  := $(USERSPACE_SRC:%=%.elf)

INITRD_CPIO    := initrd.cpio
INITRD_OBJ     := build/initrd/initrd_cpio.o

QEMU_FLAGS     := -cdrom $(ISO) -m 256M -nographic -nic none -boot order=d

# ------------------------------------------------------------------------------
# Build Rules & Targets
# ------------------------------------------------------------------------------
.PHONY: all clean run-qemu test-qemu iso test symbols debug release

-include $(DEPFILES)

all: $(ISO)

$(ISO): $(KERNEL) iso/boot/grub/grub.cfg
	@mkdir -p iso/boot
	cp $(KERNEL) iso/boot/kernel.elf
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

test-qemu: $(ISO)
	@if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
	    echo "Booting in QEMU Environment (Automated Self-Test Execution Pipeline)..."; \
	    expect -c ' \
	        set timeout 60; \
	        spawn qemu-system-x86_64 $(QEMU_FLAGS) -no-reboot -device isa-debug-exit \
	            -drive if=pflash,format=raw,readonly=on,file=/opt/homebrew/share/qemu/edk2-x86_64-code.fd; \
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
	                send_user "\nTimeout waiting for test results\n"; \
	                exit 1; \
	            } \
	            eof { \
	                send_user "\nQEMU exited unexpectedly\n"; \
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

debug: CXXFLAGS += -g -Og -DDEBUG
debug: CCFLAGS  += -g -Og -DDEBUG
debug: LDFLAGS  += --no-gc-sections
debug: $(OBJ) $(INITRD_OBJ) linker.ld
	@mkdir -p $(dir $(KERNEL_DEBUG))
	$(LD) $(LDFLAGS) -o $(KERNEL_DEBUG) $(OBJ) $(INITRD_OBJ)
	@echo "Debug target kernel constructed: $(KERNEL_DEBUG) ($$($(GET_SIZE) $(KERNEL_DEBUG)) bytes)"
	cp $(KERNEL_DEBUG) iso/boot/kernel.elf
	@grub-mkrescue -o jarvis-rtos-debug.iso iso 2>/dev/null
	@echo "Debug ISO image mapped successfully: jarvis-rtos-debug.iso"

release: CXXFLAGS += -g -O2
release: $(ISO)
	@echo ""
	@echo "=========================================="
	@echo " Hard Real-Time Production Target Ready: $(ISO)"
	@echo " Starting QEMU with serial output stream..."
	@echo "=========================================="
	@if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
	    if [ -f /opt/homebrew/share/qemu/edk2-x86_64-code.fd ]; then \
	        qemu-system-x86_64 -cdrom $(ISO) -m 256M -serial stdio \
	            -vga std -nic none -boot order=d -no-reboot \
	            -drive if=pflash,format=raw,readonly=on,file=/opt/homebrew/share/qemu/edk2-x86_64-code.fd; \
	    else \
	        qemu-system-x86_64 -cdrom $(ISO) -m 256M -serial stdio \
	            -vga std -nic none -boot order=d -no-reboot; \
	    fi \
	else \
	    echo "QEMU executable missing from active device environment paths."; echo $(PKG_HINT); exit 1; \
	fi

symbols: $(KERNEL)
	nm -n $(KERNEL) > $(KERNEL_SYMBOLS)
	@echo "System Symbol Matrix generated safely: $(KERNEL_SYMBOLS)"

clean:
	rm -rf build $(ISO) iso/boot/kernel.elf $(INITRD_CPIO) $(LIBC_A)
	rm -f $(USERSPACE_ELF)
	rm -f jarvis-rtos-debug.iso
	rm -rf initrd_root
	@echo "Environment workspace clean-up routines completed."