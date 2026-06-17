# ==============================================================================
# Shared build rules — included by top-level Makefile
#
# Paths in this file are relative to the including Makefile (project root).
# Variables set by the parent before include (CXXFLAGS, CCFLAGS, ASFLAGS,
# LDFLAGS, KERNEL, KERNEL_DEBUG, ...) are available here.
# ==============================================================================

# ------------------------------------------------------------------------------
# Source discovery & object file mapping
# ------------------------------------------------------------------------------
SRC_CXX        := $(shell find src -name '*.cpp')
SRC_ASM        := $(shell find src -name '*.asm')
OBJ            := $(SRC_CXX:src/%.cpp=build/%.o) $(SRC_ASM:src/%.asm=build/%.o)
DEPFILES       := $(shell find build -name '*.d' 2>/dev/null)
-include $(DEPFILES)

# ------------------------------------------------------------------------------
# Libc (shared artifact, same .o files for both builds)
# ------------------------------------------------------------------------------
LIBC_DIR       := src/libc
LIBC_SRC       := $(shell find $(LIBC_DIR) -name '*.c' -o -name '*.S')
LIBC_OBJ       := $(patsubst src/%.c,build/%.o,$(filter %.c,$(LIBC_SRC))) \
                  $(patsubst src/%.S,build/%.o,$(filter %.S,$(LIBC_SRC)))
LIBC_A         := build/libc/libc.a

USERSPACE_SRC  := $(shell find userspace -name '*.S' -o -name '*.c' 2>/dev/null)
USERSPACE_ELF  := $(USERSPACE_SRC:%=%.elf)

INITRD_CPIO    := initrd.cpio
INITRD_OBJ     := build/initrd/initrd_cpio.o

FAT32_IMG      := build/fat32.img
FAT32_OBJ      := build/fat32/fat32_img.o

# ------------------------------------------------------------------------------
# Pattern rules
# ------------------------------------------------------------------------------
build/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	@printf '  %-7s %s\n' 'CC' '$@'
	$(CXX) $(CXXFLAGS) -c -o $@ $<

build/%.o: src/%.asm
	@mkdir -p $(dir $@)
	@printf '  %-7s %s\n' 'AS' '$@'
	$(AS) $(ASFLAGS) -o $@ $<

# ------------------------------------------------------------------------------
# Libc rules
# ------------------------------------------------------------------------------
build/libc/%.o: src/libc/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CCFLAGS) -I src/libc -c -o $@ $<

build/libc/%.o: src/libc/%.S
	@mkdir -p $(dir $@)
	$(CC) $(CCFLAGS) -c -o $@ $<

$(LIBC_A): $(LIBC_OBJ)
	@printf '  %-7s %s\n' 'AR' 'libc.a'
	$(AR) rcs $@ $^

# ------------------------------------------------------------------------------
# Userspace ELFs
# ------------------------------------------------------------------------------
userspace/%.S.elf: userspace/%.S
	@printf '  %-7s %s\n' 'CC' '$@'
	$(CC) $(CCFLAGS) -o $@ $<

userspace/%.c.elf: userspace/%.c $(LIBC_A) build/libc/crt0.o
	@printf '  %-7s %s\n' 'CC' '$@'
	$(CC) $(CCFLAGS) -I src/libc -o $@ build/libc/crt0.o $< -L build/libc -lc

# ------------------------------------------------------------------------------
# Initrd
# ------------------------------------------------------------------------------
$(INITRD_CPIO): $(USERSPACE_ELF)
	@printf '  %-7s %s\n' 'CPIO' 'initrd.cpio'
	@mkdir -p initrd_root
	@if [ ! -z "$(USERSPACE_ELF)" ]; then cp $(USERSPACE_ELF) initrd_root/; fi
	cd initrd_root && find . -print0 | cpio -o -H newc -0 --quiet > ../$@
	@rm -rf initrd_root

$(INITRD_OBJ): $(INITRD_CPIO)
	@printf '  %-7s %s\n' 'OBJCOPY' '$@'
	@mkdir -p $(dir $@)
	x86_64-elf-objcopy -I binary -O elf64-x86-64 -B i386 $< $@

# ------------------------------------------------------------------------------
# FAT32 disk image
# ------------------------------------------------------------------------------
$(FAT32_IMG): tools/mkfat32img.py
	@printf '  %-7s %s\n' 'FATIMG' '$@'
	@mkdir -p $(dir $@)
	python3 tools/mkfat32img.py $@

$(FAT32_OBJ): $(FAT32_IMG)
	@printf '  %-7s %s\n' 'OBJCOPY' '$@'
	@mkdir -p $(dir $@)
	x86_64-elf-objcopy -I binary -O elf64-x86-64 -B i386 $< $@

# ------------------------------------------------------------------------------
# Kernel link rules
#
# Both debug and release use the same LDFLAGS and same .o files.  The
# difference in build type is driven entirely by CXXFLAGS on the .o files.
# ------------------------------------------------------------------------------
$(KERNEL_DEBUG): $(OBJ) $(INITRD_OBJ) $(FAT32_OBJ) linker.ld
	@mkdir -p $(dir $@)
	@printf '  %-7s %s\n' 'LD' 'kernel-debug.elf'
	$(LD) $(LDFLAGS) -o $@ $(OBJ) $(INITRD_OBJ) $(FAT32_OBJ)
	@printf '  %-7s %s\n' 'CRC' 'Patching code CRC…'
	@python3 tools/patch_code_crc.py $@
	@printf '  %-7s %s\n' 'SIZE' "$$($(GET_SIZE) $@) bytes"

$(KERNEL): $(OBJ) $(INITRD_OBJ) $(FAT32_OBJ) linker.ld
	@mkdir -p $(dir $@)
	@printf '  %-7s %s\n' 'LD' 'kernel.elf'
	$(LD) $(LDFLAGS) -o $@ $(OBJ) $(INITRD_OBJ) $(FAT32_OBJ)
	@printf '  %-7s %s\n' 'CRC' 'Patching code CRC…'
	@python3 tools/patch_code_crc.py $@
	@printf '  %-7s %s\n' 'SIZE' "$$($(GET_SIZE) $@) bytes"

# ------------------------------------------------------------------------------
# ISO boot helper
# ------------------------------------------------------------------------------
iso/boot:
	@mkdir -p $@
