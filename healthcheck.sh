#!/usr/bin/env bash
# ==============================================================================
# Jarvis RTOS Pre-Flight Health Check & Environment Verification Script
# Target: Cross-Platform (Linux/macOS) Validation for Autonomous Agents
# ==============================================================================

set -euo pipefail

# Visual Formatting Colors
REG='\033[0m'
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'

echo -e "${BLUE}======================================================================${REG}"
echo -e "${BLUE}        LAUNCHING JARVIS RTOS AGENT ENVIRONMENT HEALTH CHECK          ${REG}"
echo -e "${BLUE}======================================================================${REG}"

# 1. Detect Host Operating System
OS_TYPE=$(uname -s)
echo -e "Host Architecture Detected: ${YELLOW}${OS_TYPE}${REG}"

# 2. Establish Package Manager Context & Tool Overrides
case "${OS_TYPE}" in
    Darwin)
        REQUIRED_BINARIES=("x86_64-elf-gcc" "x86_64-elf-ld" "nasm" "qemu-system-x86_64" "grub-mkrescue" "xorriso" "tree" "cpio")
        PKG_MANAGER_HINT="brew install x86_64-elf-gcc x86_64-elf-binutils nasm qemu xorriso tree"
        ;;
    Linux)
        REQUIRED_BINARIES=("x86_64-elf-gcc" "x86_64-elf-ld" "nasm" "qemu-system-x86_64" "grub-mkrescue" "xorriso" "tree" "cpio")
        PKG_MANAGER_HINT="sudo apt-get update && sudo apt-get install -y build-essential qemu-system-x86 nasm xorriso grub-common tree cpio"
        ;;
    *)
        echo -e "${RED}[ERROR] Unsupported host operating system platform: ${OS_TYPE}${REG}"
        exit 1
        ;;
esac

# 3. Verify System Path Execution & Binaries
MISSING_TOOLS=0
echo -e "\nEvaluating vital toolchain binaries..."

for binary in "${REQUIRED_BINARIES[@]}"; do
    if command -v "$binary" >/dev/null 2>&1; then
        BINARY_PATH=$(command -v "$binary")
        echo -e "  [${GREEN}OK${REG}] Found ${GREEN}${binary}${REG} at: ${BINARY_PATH}"
    else
        echo -e "  [${RED}MISSING${REG}] Global dependency constraint unfulfilled: ${RED}${binary}${REG}"
        MISSING_TOOLS=$((MISSING_TOOLS + 1))
    fi
done

# 4. Enforce Cross-Platform Workspace Integrity Checks
echo -e "\nEvaluating filesystem context paths..."
if [ -d "$HOME/jarvis" ]; then
    echo -e "  [${GREEN}OK${REG}] Workspace root located securely at: ${GREEN}$HOME/jarvis${REG}"
else
    echo -e "  [${RED}FAIL${REG}] Active workspace structure could not be mapped to expected directory ($HOME/jarvis)."
    MISSING_TOOLS=$((MISSING_TOOLS + 1))
fi

# 5. Evaluate Environment Action Barriers
echo -e "\n======================================================================"
if [ ${MISSING_TOOLS} -gt 0 ]; then
    echo -e "${RED}[BLOCK] Real-time compilation environment contains broken dependencies.${REG}"
    echo -e "${YELLOW}Suggested Remediations:${REG}"
    echo -e "  Execute: ${BLUE}${PKG_MANAGER_HINT}${REG}"
    echo -e "  Additionally ensure cross-compilers are exported to your user active PATH variables."
    echo -e "${BLUE}======================================================================${REG}"
    exit 1
else
    echo -e "${GREEN}[SUCCESS] All development boundaries match strict architectural baselines.${REG}"
    echo -e "          The autonomous software engineering agent can safely proceed."
    echo -e "${BLUE}======================================================================${REG}"
    exit 0
fi
