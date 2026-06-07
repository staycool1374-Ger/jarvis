#!/bin/bash
# ==============================================================================
# Jarvis RTOS OpenCode Cross-Platform Execution Wrapper
# Model Target: Nemotron 3 Ultra Free (High Reasoning Variant)
# ==============================================================================

set -euo pipefail

# Dynamic user home pathing resolution
WORKSPACE_DIR="$HOME/jarvis/os"
PROMPT_FILE="PROMPT.md"

# 1. Structural Sanity Checks
if [ ! -d "$WORKSPACE_DIR" ]; then
    echo "ERROR: Project target subfolder not found at: $WORKSPACE_DIR"
    exit 1
fi

if [ ! -f "$PROMPT_FILE" ]; then
    echo "ERROR: Baseline system instructions missing. Ensure $PROMPT_FILE is in this directory."
    exit 1
fi

# Force-override any persistent environment defaults
unset OPENCODE_MODEL
unset MODEL_FALLBACK

# 2. Launching OpenCode
echo "Synchronizing environment with opencode/nemotron-3-ultra-free..."
OPENCODE_MODEL="opencode/nemotron-3-ultra-free" opencode \
    --model "opencode/nemotron-3-ultra-free" \
    --prompt "$(< "$PROMPT_FILE")" \
    --continue \
    "$WORKSPACE_DIR"



