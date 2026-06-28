# ==============================================================================
# Shared build rules — included by top-level Makefile
#
# Paths in this file are relative to the including Makefile (project root).
# Variables set by the parent before include (CXXFLAGS, CCFLAGS, ASFLAGS,
# LDFLAGS, KERNEL, KERNEL_DEBUG, ...) are available here.
# ==============================================================================

# ------------------------------------------------------------------------------
# Source discovery & object file mapping
#
# Generic sources (outside arch/) + per-ARCH sources under arch/$(ARCH)/
# ------------------------------------------------------------------------------
SRC_CXX_GENERIC := $(shell find src -path '*/kernel/arch' -prune -o -name '*.cpp' -print)
SRC_ASM_GENERIC := $(shell find src -path '*/kernel/arch' -prune -o -name '*.asm' -print)
# Exclude x86-specific NASM files for non-x86 architectures
ifneq ($(ARCH),x86_64)
SRC_ASM_GENERIC := $(filter-out src/kernel/syscall/syscall_entry.asm, $(SRC_ASM_GENERIC))
SRC_ASM_GENERIC := $(filter-out src/kernel/arch/%, $(SRC_ASM_GENERIC))
endif
# Exclude x86_64 FPU/SSE test files that fail to compile with GCC 16
ifeq ($(ARCH),x86_64)
SRC_CXX_GENERIC := $(filter-out src/kernel/test/test_fpu.cpp src/kernel/test/test_fpu_clone.cpp src/kernel/test/test_fpu_multi.cpp src/kernel/test/test_fpu_sse.cpp src/kernel/test/test_fpu_xmm_all.cpp, $(SRC_CXX_GENERIC))
endif
SRC_S_GENERIC   := $(shell find src -path '*/kernel/arch' -prune -o -path '*/libc' -prune -o -name '*.S' -print)

SRC_CXX_ARCH    := $(shell find src/kernel/arch/$(ARCH) -name '*.cpp' 2>/dev/null)
SRC_ASM_ARCH    := $(shell find src/kernel/arch/$(ARCH) -name '*.asm' 2>/dev/null)
SRC_S_ARCH      := $(shell find src/kernel/arch/$(ARCH) -name '*.S' 2>/dev/null)

SRC_CXX_ARCH_BASE := $(shell find src/kernel/arch -maxdepth 1 -name '*.cpp' 2>/dev/null)
SRC_CXX         := $(SRC_CXX_GENERIC) $(SRC_CXX_ARCH_BASE) $(SRC_CXX_ARCH)
SRC_ASM         := $(SRC_ASM_GENERIC) $(SRC_ASM_ARCH)
SRC_S           := $(SRC_S_GENERIC) $(SRC_S_ARCH)
OBJ             := $(SRC_CXX:src/%.cpp=build/%.o) $(SRC_ASM:src/%.asm=build/%.o) $(SRC_S:src/%.S=build/%.o)
DEPFILES        := $(shell find build -name '*.d' 2>/dev/null)
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

build/%.o: src/%.S
	@mkdir -p $(dir $@)
	@printf '  %-7s %s\n' 'AS' '$@'
	$(CC) $(CCFLAGS) -c -o $@ $<

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
$(INITRD_CPIO): $(USERSPACE_ELF) initrd/tests/test-config.txt
	@printf '  %-7s %s\n' 'CPIO' 'initrd.cpio'
	@mkdir -p initrd_root/etc initrd_root/tmp initrd_root/tests
	@printf 'tmpfs /tmp\n' > initrd_root/etc/fstab
	@printf '#!/bin/sh\n' > initrd_root/etc/rc
	@printf '# Init script\n' >> initrd_root/etc/rc
	@if [ ! -z "$(USERSPACE_ELF)" ]; then cp $(USERSPACE_ELF) initrd_root/; fi
	cp initrd/tests/test-config.txt initrd_root/tests/test-config.txt
	cd initrd_root && find . -print0 | cpio -o -H newc -0 --quiet > ../$@
	@rm -rf initrd_root

$(INITRD_OBJ): $(INITRD_CPIO)
	@printf '  %-7s %s\n' 'OBJCOPY' '$@'
	@mkdir -p $(dir $@)
	$(OBJCOPY) -I binary -O $(OBJCOPY_FMT) -B $(OBJCOPY_ARCH) $< $@

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
	$(OBJCOPY) -I binary -O $(OBJCOPY_FMT) -B $(OBJCOPY_ARCH) $< $@

# ------------------------------------------------------------------------------
# Kernel link rules
#
# Both debug and release use the same LDFLAGS and same .o files.  The
# difference in build type is driven entirely by CXXFLAGS on the .o files.
# ------------------------------------------------------------------------------
ARCH_STAMP := build/.arch-stamp

# .PHONY check that triggers clean when ARCH changes
.PHONY: check-arch
check-arch:
	@stamp_arch=$$(cat $(ARCH_STAMP) 2>/dev/null); \
	if [ "$$stamp_arch" != "$(ARCH)" ] && [ -n "$$stamp_arch" ]; then \
	    printf '  %-7s %s\n' 'CLEAN' "Architecture changed ($$stamp_arch -> $(ARCH))"; \
	    $(MAKE) clean >/dev/null 2>&1; \
	    $(MAKE) $(MAKECMDGOALS) >/dev/null 2>&1; \
	    exit $$?; \
	fi; \
	mkdir -p $$(dirname $(ARCH_STAMP)); echo $(ARCH) > $(ARCH_STAMP)

$(KERNEL_DEBUG): $(OBJ) $(INITRD_OBJ) $(FAT32_OBJ) check-arch linker_$(ARCH).ld
	@mkdir -p $(dir $@)
	@printf '  %-7s %s\n' 'LD' 'kernel-debug.elf'
	@$(LD) $(LDFLAGS) -o $@ $(OBJ) $(INITRD_OBJ) $(FAT32_OBJ) $(LD_LIBS)
	@printf '  %-7s %s\n' 'CRC' 'Patching code CRC…'
	@python3 tools/patch_code_crc.py $@
	@printf '  %-7s %s\n' 'SIZE' "$$($(GET_SIZE) $@) bytes"

$(KERNEL): $(OBJ) $(INITRD_OBJ) $(FAT32_OBJ) check-arch linker_$(ARCH).ld
	@mkdir -p $(dir $@)
	@printf '  %-7s %s\n' 'LD' 'kernel.elf'
	@$(LD) $(LDFLAGS) -o $@ $(OBJ) $(INITRD_OBJ) $(FAT32_OBJ) $(LD_LIBS)
	@printf '  %-7s %s\n' 'CRC' 'Patching code CRC…'
	@python3 tools/patch_code_crc.py $@
	@printf '  %-7s %s\n' 'SIZE' "$$($(GET_SIZE) $@) bytes"

# ------------------------------------------------------------------------------
# ISO boot helper
# ------------------------------------------------------------------------------
iso/boot:
	@mkdir -p $@
