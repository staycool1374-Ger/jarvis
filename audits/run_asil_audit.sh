#!/bin/bash

# ==============================================================================
# Jarvis RTOS - ASIL-D Multi-Agent Code Auditor (Multi-File Support)
# ==============================================================================

if [ -z "$OPENROUTER_API_KEY" ]; then
    echo "Error: OPENROUTER_API_KEY environment variable is not set."
    exit 1
fi

if ! command -v jq &> /dev/null; then
    echo "Error: 'jq' is not installed."
    exit 1
fi

MODEL="anthropic/claude-sonnet-5"

# ==============================================================================
# Modulspezifische ASIL-D Vorgaben
# ==============================================================================
PROMPT_MEMORY="TASK FOR MODULE: Memory Management (PMM, VMM, MemPool)
1. Fragmentation: MemPool must not fragment or leak after init.
2. Deterministic: Alloc/Free must be strict O(1) (no hidden loops).
3. Isolation: Ring-3 pages cannot be mapped as Ring-0.
4. Concurrency: SpinLocks + IRQ disable required for PMM bitmaps."

PROMPT_TASK="TASK FOR MODULE: Task Management & Scheduling (TCB, Scheduler)
1. Complexity: Task selection must be O(1) priority bitmaps (e.g., __builtin_clz), no O(n) linear search.
2. Overhead: Zero unnecessary memory accesses during context switch.
3. Inversion: TCB must enforce Priority Inheritance Protocol (PIP).
4. Stack: Guard-Pages must be enforced during stack allocation."

PROMPT_IPC="TASK FOR MODULE: IPC & Synchronization
1. Bounded Queues: Strict capacity limits for Message Queues.
2. Deadlocks: No paths where IPC and scheduler block each other.
3. Wake-up paths: O(1) thread wake-up functions (no uncontrollable iteration).
4. IRQ-Safety: EventGroup/Notify must be safe from interrupt context."

PROMPT_BOUNDARY="TASK FOR MODULE: Syscall Layer, VFS & ELF Loader
1. Validation: CheckedPtr required for every Ring-3 memory pointer.
2. TOCTOU: VFS path resolution must be safe against race conditions.
3. ELF-Safety: Loaded segments must never configure Ring-0 pages as user-executable.
4. Registry: Syscall dispatch must prevent out-of-bounds calls."

PROMPT_HARDWARE="TASK FOR MODULE: Hardware Drivers & Interrupts
1. Reentrance: IRQ handlers must lock shared data via SpinLocks.
2. Bottom-Half: Defer long-running IRQ code immediately.
3. HAL Migration: Hardware access must be delegable to Ring-3 HAL services."

# ==============================================================================
# Hilfsfunktionen
# ==============================================================================

print_usage() {
    echo "Usage: $0 <module_type> <file1> [file2] [file3] ..."
    echo "Modules: memory, task, ipc, boundary, hardware"
    echo "Example: $0 task src/scheduler.cpp include/scheduler.hpp src/tcb.cpp"
}

call_agent() {
    local system_msg="$1"
    local user_msg="$2"
    local agent_name="$3"

    echo "[>] Running $agent_name..."

    local JSON_PAYLOAD
    JSON_PAYLOAD=$(jq -n \
      --arg model "$MODEL" \
      --arg system_msg "$system_msg" \
      --arg user_msg "$user_msg" \
      '{
        "model": $model,
        "temperature": 0.1,
        "messages": [
          {"role": "system", "content": $system_msg},
          {"role": "user", "content": $user_msg}
        ]
      }')

    local RESPONSE
    RESPONSE=$(curl -s -X POST "https://openrouter.ai/api/v1/chat/completions" \
      -H "Authorization: Bearer $OPENROUTER_API_KEY" \
      -H "Content-Type: application/json" \
      -H "HTTP-Referer: https://github.com/staycool1374-Ger/nexios" \
      -H "X-Title: Jarvis RTOS Code Auditor" \
      -d "$JSON_PAYLOAD")

    if [ $? -ne 0 ]; then
        echo "Error: API connection failed during $agent_name."
        exit 1
    fi

    local AI_CONTENT
    AI_CONTENT=$(echo "$RESPONSE" | jq -r '.choices[0].message.content')

    if [ "$AI_CONTENT" = "null" ] || [ -z "$AI_CONTENT" ]; then
        echo "Error: API returned invalid response during $agent_name. Raw Response:"
        echo "$RESPONSE" | jq .
        exit 1
    fi

    echo "$AI_CONTENT"
}

# ==============================================================================
# Hauptlogik
# ==============================================================================

if [ "$#" -lt 2 ]; then
    print_usage
    exit 1
fi

MODULE_TYPE=$1
shift # Verschiebt die Argumente, sodass $@ jetzt nur noch die Dateipfade enthält

case "$MODULE_TYPE" in
    memory)   MODULE_RULES="$PROMPT_MEMORY" ;;
    task)     MODULE_RULES="$PROMPT_TASK" ;;
    ipc)      MODULE_RULES="$PROMPT_IPC" ;;
    boundary) MODULE_RULES="$PROMPT_BOUNDARY" ;;
    hardware) MODULE_RULES="$PROMPT_HARDWARE" ;;
    *) echo "Error: Unknown module '$MODULE_TYPE'."; print_usage; exit 1 ;;
esac

# ------------------------------------------------------------------------------
# Dateien einlesen und zusammenbauen
# ------------------------------------------------------------------------------
CODE_CONTENT=""
FILE_COUNT=0

for FILE in "$@"; do
    if [ ! -f "$FILE" ]; then
        echo "Error: File '$FILE' not found."
        exit 1
    fi
    
    # Dateiinhalt an den Code-String anhängen, visuell durch Trennlinien separiert
    FILE_CONTENT=$(cat "$FILE")
    CODE_CONTENT="${CODE_CONTENT}--- FILE: ${FILE} ---\n${FILE_CONTENT}\n\n"
    FILE_COUNT=$((FILE_COUNT + 1))
done

# Generiere einen sinnvollen Namen für den Output-Report basierend auf dem ersten File
FIRST_FILE=$(basename "$1")
BASE_NAME="${FIRST_FILE%.*}"
AUDIT_DIR="audits"
mkdir -p "$AUDIT_DIR"
OUTPUT_FILE="${AUDIT_DIR}/audit_${MODULE_TYPE}_${BASE_NAME}_and_others.md"

echo "[*] Starting customized Jarvis ASIL-D architectural audit for $FILE_COUNT files..."
echo "[*] Output will be saved to: $OUTPUT_FILE"

# ------------------------------------------------------------------------------
# AGENT 1: Environmental Isolation
# ------------------------------------------------------------------------------
SYS_1="You are an isolation agent for the Jarvis RTOS freestanding kernel (C++20). Your task is to identify and list every macro (CONFIG_*), every type of locking primitive, and every raw pointer usage in the provided source files."
PROMPT_1="Identify all configuration flags, locking primitives, and raw pointer usages in this codebase:\n\n${CODE_CONTENT}"

RESULT_1=$(call_agent "$SYS_1" "$PROMPT_1" "Agent 1: Environmental Isolation")

# ------------------------------------------------------------------------------
# AGENT 2: The Ruthless Hard-RT Attacker
# ------------------------------------------------------------------------------
SYS_2="You are a critical system safety auditor for Jarvis RTOS (Hard Real-Time, ASIL-D level). Your sole mission is to find architectural violations. You must flag code that violates these strict invariants:
1. DEPRECATED GLOBAL IRQGUARD: The use of global 'IrqGuard' is strictly forbidden. Code must use fine-grained 'SpinLock' + 'SpinLockGuard' or 'sync::Mutex' without IrqGuard.
2. RAW POINTERS VS REFERENCES: Task blocks (TCB) and IPC endpoints must pass by reference to prevent dangling lookups and compiler stack slot reuse optimizations from corrupting data under -O3.
3. WCET VIOLATIONS: Loops must have bounded iterations (no open-ended while loops in hot paths).
4. UNDEFINED BEHAVIOR: Guard hardware builtins (like __builtin_clzll) against zero inputs.
Be extremely aggressive. If a violation is suspected, report it."

PROMPT_2="Analyze the code for architectural flaws. Apply the core invariants AND the following module-specific rules:\n${MODULE_RULES}\n\n--- CODE TO AUDIT ---\n${CODE_CONTENT}\n\n--- CONTEXT FROM AGENT 1 ---\n${RESULT_1}"

RESULT_2=$(call_agent "$SYS_2" "$PROMPT_2" "Agent 2: Safety & Invariant Attacker")

# ------------------------------------------------------------------------------
# AGENT 3: Kernel Synthesizer (Output formatted for Fixing-AI)
# ------------------------------------------------------------------------------
SYS_3="You are the Lead Kernel Architect of Jarvis RTOS. You filter out false positives from the attacker's report and output a highly structured, machine-readable audit report in Markdown format.
CRITICAL: This output will be fed directly into ANOTHER AI AGENT responsible for writing the code fixes. 
Format your output as a strict checklist of flaws. For every verified flaw, include:
- [ ] VULNERABILITY ID
- EXACT LINE(S) OR FUNCTION (Include the filename provided in the context)
- ROOT CAUSE (Why it fails ASIL-D)
- REQUIRED FIX (Explicit C++20 freestanding instructions for the next AI on how to resolve it, including zero-allocation constraints). Do NOT write the full refactored file, just the technical directive for the fixing AI."

PROMPT_3="Review the attacker's findings against the original code. Filter out false positives, and output the final Markdown instruction report for the fixing AI.\n\nOriginal Code:\n${CODE_CONTENT}\n\nAttacker Findings:\n${RESULT_2}"

FINAL_REPORT=$(call_agent "$SYS_3" "$PROMPT_3" "Agent 3: Kernel Synthesizer")

echo "$FINAL_REPORT" > "$OUTPUT_FILE"

echo "================================================================================"
echo "[+] Audit complete! AI-readable fix report compiled into: $OUTPUT_FILE"
echo "================================================================================"
