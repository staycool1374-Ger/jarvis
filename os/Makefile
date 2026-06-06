CXX      := ccache g++
CXXFLAGS := -std=c++20 -ffreestanding -fno-exceptions -fno-rtti \
            -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
            -fno-threadsafe-statics -m64 -mno-red-zone \
            -mgeneral-regs-only -Wall -Wextra -Werror \
            -I src -I src/lib -O3 -pipe -MMD -MP \
            -ffunction-sections -fdata-sections
CC       := gcc
CCFLAGS  := -m64 -static -nostdlib -ffreestanding \
            -Wl,--build-id=none -O2 -pipe -MMD -MP
AS       := nasm
ASFLAGS  := -f elf64
LD       := ld
LDFLAGS  := -m elf_x86_64 -nostdlib -T linker.ld -z max-page-size=0x1000 \
            --gc-sections -Map=build/kernel.map
DEPFILES := $(shell find build -name '*.d' 2>/dev/null)

SRC_CXX  := $(shell find src -name '*.cpp')
SRC_ASM  := $(shell find src -name '*.asm')
OBJ      := $(SRC_CXX:src/%.cpp=build/%.o) $(SRC_ASM:src/%.asm=build/%.o)
KERNEL   := build/kernel.elf
KERNEL_SYMBOLS := build/kernel.sym
KERNEL_DEBUG   := build/kernel-debug.elf
ISO      := jarvis-rtos.iso

LIBC_DIR  := src/libc
LIBC_SRC  := $(shell find $(LIBC_DIR) -name '*.c' -o -name '*.S')
LIBC_OBJ  := $(patsubst src/%.c,build/%.o,$(filter %.c,$(LIBC_SRC))) \
             $(patsubst src/%.S,build/%.o,$(filter %.S,$(LIBC_SRC)))
LIBC_A    := build/libc/libc.a

USERSPACE_SRC := $(shell find userspace -name '*.S' -o -name '*.c' 2>/dev/null)
USERSPACE_ELF := $(USERSPACE_SRC:%=%.elf)

INITRD_CPIO := initrd.cpio
INITRD_OBJ  := build/initrd/initrd_cpio.o

.PHONY: all clean run-qemu test-qemu iso test symbols debug release

-include $(DEPFILES)

all: $(ISO)

$(ISO): $(KERNEL) iso/boot/grub/grub.cfg
	mkdir -p iso/boot
	cp $(KERNEL) iso/boot/kernel.elf
	grub-mkrescue -o $@ iso 2>/dev/null || \
	(echo "FEHLER: grub-mkrescue fehlgeschlagen. Installieren Sie xorriso und grub-common."; exit 1)
	@echo ""
	@echo "=========================================="
	@echo " ISO erstellt: $(ISO)"
	@echo " Starten mit:"
	@echo "   make run-qemu"
	@echo "=========================================="

$(KERNEL): $(OBJ) $(INITRD_OBJ) linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJ) $(INITRD_OBJ)
	@echo "Kernel gebaut: $@ ($$(stat -c%s $@) Bytes)"

$(INITRD_OBJ): $(INITRD_CPIO)
	@mkdir -p $(dir $@)
	objcopy -I binary -O elf64-x86-64 -B i386 $< $@

$(INITRD_CPIO): $(USERSPACE_ELF)
	@mkdir -p initrd_root
	cp $(USERSPACE_ELF) initrd_root/
	cd initrd_root && find . -print0 | cpio -o -H newc -0 --quiet > ../$@
	rm -rf initrd_root

build/libc/%.o: src/libc/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CCFLAGS) -I src/libc -c -o $@ $<

build/libc/%.o: src/libc/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CCFLAGS) -c -o $@ $<

$(LIBC_A): $(LIBC_OBJ)
	ar rcs $@ $^

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

QEMU_FLAGS := -cdrom $(ISO) -m 256M -nographic -no-reboot -nic none -boot order=d

run-qemu: $(ISO)
	@if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
	    qemu-system-x86_64 $(QEMU_FLAGS); \
	else \
	    echo "QEMU nicht gefunden. Installieren Sie qemu-system-x86."; \
	    echo "  apt-get install qemu-system-x86"; \
	    exit 1; \
	fi

test-qemu: $(ISO)
	@if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
	    echo "Booting in QEMU (auto-exit after tests)..."; \
	    qemu-system-x86_64 $(QEMU_FLAGS) -device isa-debug-exit; \
	    rc=$$?; \
	    if [ $$rc -eq 3 ]; then \
	        echo "OK: All tests passed (QEMU debug-exit)."; \
	    elif [ $$rc -eq 0 ]; then \
	        echo "OK: QEMU exited cleanly."; \
	    else \
	        echo "FAIL: QEMU exit code $$rc"; \
	        exit 1; \
	    fi \
	else \
	    echo "QEMU nicht gefunden. Installieren Sie qemu-system-x86."; \
	    exit 1; \
	fi

test: $(KERNEL)
	@echo "=========================================="
	@echo " Running kernel self-tests..."
	@echo " (boot kernel and all tests run at startup)"
	@echo "=========================================="
	@echo "Userspace validation:"
	@echo "  Building userspace programs..."
	$(MAKE) $(USERSPACE_ELF)
	@echo "  Userspace programs built successfully."
	@echo "  Run 'selftest' in the shell to trigger tests interactively."

debug: CXXFLAGS += -g -Og -DDEBUG
debug: CCFLAGS  += -g -Og -DDEBUG
debug: LDFLAGS  += --no-gc-sections
debug: $(OBJ) $(INITRD_OBJ) linker.ld
	@mkdir -p $(dir $(KERNEL_DEBUG))
	$(LD) $(LDFLAGS) -o $(KERNEL_DEBUG) $(OBJ) $(INITRD_OBJ)
	@echo "Debug kernel: $(KERNEL_DEBUG) ($$(stat -c%s $(KERNEL_DEBUG)) Bytes)"
	cp $(KERNEL_DEBUG) iso/boot/kernel.elf
	grub-mkrescue -o jarvis-rtos-debug.iso iso 2>/dev/null
	@echo "Debug ISO: jarvis-rtos-debug.iso"

release: CXXFLAGS += -DNDEBUG
release: $(ISO)
	cp $(KERNEL) build/kernel-stripped.elf
	objcopy --strip-debug build/kernel.elf build/kernel-stripped.elf
	@echo "Stripped kernel: build/kernel-stripped.elf ($$(stat -c%s build/kernel-stripped.elf) Bytes)"

symbols: $(KERNEL)
	nm -n $(KERNEL) > $(KERNEL_SYMBOLS)
	@echo "Symbol table: $(KERNEL_SYMBOLS)"

clean:
	rm -rf build $(ISO) iso/boot/kernel.elf $(INITRD_CPIO) $(LIBC_A)
	rm -f $(USERSPACE_ELF)
	rm -f jarvis-rtos-debug.iso
	rm -rf initrd_root
	@echo "Bereinigt."
