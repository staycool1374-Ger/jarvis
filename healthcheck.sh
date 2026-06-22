#!/usr/bin/env bash
# ==============================================================================
# Jarvis RTOS Pre-Flight Health Check & Environment Verification Script
# Target: Cross-Platform (Linux/macOS) Validation for Autonomous Agents
#
# Usage: bash healthcheck.sh [arch]
#   arch: all (default), x86_64, aarch64, riscv64
# ==============================================================================

set -euo pipefail

REG='\033[0m'
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'

echo -e "${BLUE}======================================================================${REG}"
echo -e "${BLUE}        LAUNCHING JARVIS RTOS AGENT ENVIRONMENT HEALTH CHECK          ${REG}"
echo -e "${BLUE}======================================================================${REG}"

TARGET_ARCH="${1:-all}"

# 1. Detect Host Operating System
OS_TYPE=$(uname -s)
ARCH_UPPER=$(echo "${TARGET_ARCH}" | tr a-z A-Z)
echo -e "Host OS: ${YELLOW}${OS_TYPE}${REG}  Target Arch: ${YELLOW}${ARCH_UPPER}${REG}"

# 2. Define per-architecture toolchains
X86_64_BINS=("x86_64-elf-gcc" "x86_64-elf-ld" "nasm" "qemu-system-x86_64")
AARCH64_BINS=("aarch64-elf-gcc" "aarch64-elf-ld" "qemu-system-aarch64")
RISCV64_BINS=("riscv64-unknown-elf-gcc" "riscv64-unknown-elf-ld" "qemu-system-riscv64")
SHARED_BINS=("grub-mkrescue" "xorriso" "tree" "cpio")

REQUIRED_BINARIES=()
if [ "$TARGET_ARCH" = "all" ] || [ "$TARGET_ARCH" = "x86_64" ]; then
    REQUIRED_BINARIES+=("${X86_64_BINS[@]}")
fi
if [ "$TARGET_ARCH" = "all" ] || [ "$TARGET_ARCH" = "aarch64" ]; then
    REQUIRED_BINARIES+=("${AARCH64_BINS[@]}")
fi
if [ "$TARGET_ARCH" = "all" ] || [ "$TARGET_ARCH" = "riscv64" ]; then
    REQUIRED_BINARIES+=("${RISCV64_BINS[@]}")
fi
REQUIRED_BINARIES+=("${SHARED_BINS[@]}")

# 3. Package manager hints per OS
case "${OS_TYPE}" in
    Darwin)
        PKG_HINT_X86="brew install x86_64-elf-gcc x86_64-elf-binutils nasm qemu"
        PKG_HINT_AARCH64="brew install aarch64-elf-gcc aarch64-elf-binutils"
        PKG_HINT_RISCV64="brew install riscv64-unknown-elf-gcc riscv64-unknown-elf-binutils"
        PKG_HINT_SHARED="brew install xorriso tree cpio"
        PKG_HINT_GRUB="brew install grub"
        ;;
    Linux)
        PKG_HINT_X86="sudo apt-get install -y gcc-x86-64-elf binutils-x86-64-elf nasm qemu-system-x86"
        PKG_HINT_AARCH64="sudo apt-get install -y gcc-aarch64-elf binutils-aarch64-elf qemu-system-arm"
        PKG_HINT_RISCV64="sudo apt-get install -y gcc-riscv64-unknown-elf binutils-riscv64-unknown-elf qemu-system-riscv64"
        PKG_HINT_SHARED="sudo apt-get install -y xorriso grub-common tree cpio"
        PKG_HINT_GRUB="sudo apt-get install -y grub-pc-bin"
        ;;
    *)
        echo -e "${RED}[ERROR] Unsupported host OS: ${OS_TYPE}${REG}"
        exit 1
        ;;
esac

# 4. Verify required binaries
MISSING_TOOLS=0
echo -e "\nEvaluating toolchain binaries..."

for binary in "${REQUIRED_BINARIES[@]}"; do
    if command -v "$binary" >/dev/null 2>&1; then
        echo -e "  [${GREEN}OK${REG}] ${binary}"
    else
        echo -e "  [${RED}MISSING${REG}] ${binary}"
        MISSING_TOOLS=$((MISSING_TOOLS + 1))
    fi
done

# 5. Workspace integrity
echo -e "\nEvaluating workspace context..."
if [ -d "$HOME/jarvis" ]; then
    echo -e "  [${GREEN}OK${REG}] Workspace: ${GREEN}$HOME/jarvis${REG}"
else
    echo -e "  [${RED}FAIL${REG}] Workspace directory $HOME/jarvis not found."
    MISSING_TOOLS=$((MISSING_TOOLS + 1))
fi

# 6. Result
echo -e "\n======================================================================"
if [ ${MISSING_TOOLS} -gt 0 ]; then
    echo -e "${RED}[BLOCK] ${MISSING_TOOLS} tool(s) missing for target '${TARGET_ARCH}'${REG}"
    echo -e "${YELLOW}Suggested:${REG}"
    if [ "$TARGET_ARCH" = "all" ] || [ "$TARGET_ARCH" = "x86_64" ]; then
        echo -e "  x86_64:   ${BLUE}${PKG_HINT_X86}${REG}"
    fi
    if [ "$TARGET_ARCH" = "all" ] || [ "$TARGET_ARCH" = "aarch64" ]; then
        echo -e "  aarch64:  ${BLUE}${PKG_HINT_AARCH64}${REG}"
    fi
    if [ "$TARGET_ARCH" = "all" ] || [ "$TARGET_ARCH" = "riscv64" ]; then
        echo -e "  riscv64:  ${BLUE}${PKG_HINT_RISCV64}${REG}"
    fi
    echo -e "  shared:   ${BLUE}${PKG_HINT_SHARED}${REG}"
    echo -e "  grub:     ${BLUE}${PKG_HINT_GRUB}${REG}"
    echo -e "${BLUE}======================================================================${REG}"
    exit 1
else
    echo -e "${GREEN}[SUCCESS] All tools present for target '${TARGET_ARCH}'${REG}"
    echo -e "${BLUE}======================================================================${REG}"
    exit 0
fi
