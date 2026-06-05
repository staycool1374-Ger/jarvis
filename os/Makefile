CXX      := g++
CXXFLAGS := -std=c++20 -ffreestanding -fno-exceptions -fno-rtti \
            -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
            -fno-threadsafe-statics -m64 -mno-red-zone \
            -mgeneral-regs-only -Wall -Wextra -Werror \
            -I src -I src/lib -O3 -pipe
CC       := gcc
CCFLAGS  := -m64 -static -nostdlib -ffreestanding \
            -Wl,--build-id=none -O2 -pipe
AS       := nasm
ASFLAGS  := -f elf64
LD       := ld
LDFLAGS  := -m elf_x86_64 -nostdlib -T linker.ld -z max-page-size=0x1000

SRC_CXX  := $(shell find src -name '*.cpp')
SRC_ASM  := $(shell find src -name '*.asm')
OBJ      := $(SRC_CXX:src/%.cpp=build/%.o) $(SRC_ASM:src/%.asm=build/%.o)
KERNEL   := build/kernel.elf
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

.PHONY: all clean run qemu iso

all: $(ISO)

$(ISO): $(KERNEL) iso/boot/grub/grub.cfg
	mkdir -p iso/boot
	cp $(KERNEL) iso/boot/kernel.elf
	grub-mkrescue -o $@ iso 2>/dev/null || \
	(echo "FEHLER: grub-mkrescue fehlgeschlagen. Installieren Sie xorriso und grub-common."; exit 1)
	@echo ""
	@echo "=========================================="
	@echo " ISO erstellt: $(ISO)"
	@echo " Starten mit VirtualBox oder:"
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

run-qemu: $(ISO)
	@if command -v qemu-system-x86_64 >/dev/null 2>&1; then \
	    qemu-system-x86_64 -cdrom $(ISO) -m 256M -serial stdio -vga std; \
	else \
	    echo "QEMU nicht gefunden. Installieren Sie qemu-system-x86."; \
	    echo "  apt-get install qemu-system-x86"; \
	    exit 1; \
	fi

clean:
	rm -rf build $(ISO) iso/boot/kernel.elf $(INITRD_CPIO) $(LIBC_A)
	rm -f $(USERSPACE_ELF)
	rm -rf initrd_root
	@echo "Bereinigt."
