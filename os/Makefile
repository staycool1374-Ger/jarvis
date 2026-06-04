CXX      := g++
CXXFLAGS := -std=c++20 -ffreestanding -fno-exceptions -fno-rtti \
            -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
            -fno-threadsafe-statics -m64 -mno-red-zone \
            -mgeneral-regs-only -Wall -Wextra -Werror \
            -I src -I src/lib -O2 -pipe
AS       := nasm
ASFLAGS  := -f elf64
LD       := ld
LDFLAGS  := -m elf_x86_64 -nostdlib -T linker.ld -z max-page-size=0x1000

SRC_CXX  := $(shell find src -name '*.cpp')
SRC_ASM  := $(shell find src -name '*.asm')
OBJ      := $(SRC_CXX:src/%.cpp=build/%.o) $(SRC_ASM:src/%.asm=build/%.o)
KERNEL   := build/kernel.elf
ISO      := jarvis-rtos.iso

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

$(KERNEL): $(OBJ) linker.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJ)
	@echo "Kernel gebaut: $@ ($$(stat -c%s $@) Bytes)"

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
	rm -rf build $(ISO) iso/boot/kernel.elf
	@echo "Bereinigt."
