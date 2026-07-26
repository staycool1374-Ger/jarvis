#!/bin/bash

# ==============================================================================
# RTOS Code Review - OpenRouter API Client (Auto-Header & File Output)
# ==============================================================================

# 1. Check for API Key
if [ -z "$OPENROUTER_API_KEY" ]; then
    echo "Error: OPENROUTER_API_KEY environment variable is not set."
    echo "Please set it using: export OPENROUTER_API_KEY='your_key_here'"
    exit 1
fi

# 2. Check for jq
if ! command -v jq &> /dev/null; then
    echo "Error: 'jq' is not installed. Please install it (e.g., sudo apt install jq)."
    exit 1
fi

# 3. Model Selection
MODEL="anthropic/claude-3.7-sonnet"

# ==============================================================================
# Prompts & Context
# ==============================================================================

SYSTEM_CONTEXT="You act as a Senior Embedded RTOS & Safety Engineer (ISO 26262 ASIL D / IEC 61508 SIL 4).
Your task is to analyze the provided C++20 source code of our hard real-time microkernel for vulnerabilities and output refactored, production-ready code.
The following strict rules apply to all modifications:
1. No dynamic memory allocation (new/malloc) after the initial kernel initialization.
2. All hot-paths and loops must have guaranteed upper bounds (Bounded Loops / O(1) execution time).
3. Strict avoidance of priority inversion (Use Priority Inheritance Protocol / PIP).
4. Every pointer passed from userspace (Ring 3) must be strictly validated against kernel memory boundaries.
5. SpinLocks must only be held while hardware interrupts are disabled."

PROMPT_MEMORY="TASK FOR MODULE: Memory Management (PMM, VMM, MemPool)
Analyze the attached code for physical and virtual memory management as well as the MemPool for the following deficits:
1. Fragmentation Vulnerabilities: Check if the MemPool can fragment after initialization or if memory leaks can occur upon thread termination.
2. Deterministic Execution: Analyze the allocation and free functions. Are these operations strictly O(1), or are there hidden loops over bitmaps?
3. Address Space Protection: Ensure that during VMM page table manipulation (HHDM / Direct Physical Mapping), no user-space pages (Ring 3) can be mapped or flagged as kernel pages (Ring 0).
4. Concurrency: Are there missing SpinLocks (with IRQ disabling) during parallel access to the PMM bitmap by multiple CPU cores?

OUTPUT REQUIRED:
- A numbered list of the identified vulnerabilities.
- The fully corrected and refactored C++20 code for this module."

PROMPT_TASK="TASK FOR MODULE: Task Management & Scheduling (TCB, Scheduler)
Analyze the attached scheduler and TCB code for the following hard real-time deficits:
1. Scheduling Complexity: Check the task selection function. Does it use a linear search (O(n)) over task lists? Refactor it to use O(1) priority bitmaps (e.g., using __builtin_clz).
2. Context-Switch Overhead: Identify unnecessary register saves or memory accesses during context switches.
3. Priority Inversion: Is the TaskControlBlock missing the mechanics for temporary priority inheritance (PIP)?
4. Stack Overflows: Is it guaranteed that during stack allocation for new tasks, a Guard-Page is inserted?

OUTPUT REQUIRED:
- A brief analysis of timing hazards.
- The optimized code with guaranteed O(1) scheduling decisions and Guard-Pages."

PROMPT_IPC="TASK FOR MODULE: IPC & Synchronization (Message Queue, Mutex, Semaphore)
Analyze the attached Inter-Process Communication and synchronization primitives for architecture and safety deficits:
1. Bounded Queues: Verify if Message Queues can grow indefinitely. Implement strict capacity limits.
2. Lock Hierarchies & Deadlocks: Are there paths where sending/receiving IPC and the scheduler could block each other?
3. Blocking Wait-Paths: Check if thread wake-up functions iterate uncontrollably. This must be O(1).
4. IRQ-Safety: Ensure that EventGroup/Notify primitives can be called safely from interrupt context.

OUTPUT REQUIRED:
- Identification of potential deadlock scenarios and memory exhaustion paths.
- The corrected, deadlock-free code integrated with Priority Inheritance."

PROMPT_BOUNDARY="TASK FOR MODULE: Syscall Layer, VFS & ELF Loader
Analyze the code for Syscall-Dispatch, Virtual File System, and ELF Loader for security vulnerabilities at the Ring-3 to Ring-0 boundary:
1. Pointer Validation (CheckedPtr): Identify every syscall parameter that represents a memory pointer. Implement strict checks.
2. Time-of-Check to Time-of-Use (TOCTOU): Check VFS path resolution for race conditions.
3. ELF-Loading Safety: Verify that mapping ELF segments does not configure Kernel pages as user-executable.
4. Syscall Registry: Is the dispatch table vulnerable to out-of-bounds calls?

OUTPUT REQUIRED:
- A report on found isolation breaches.
- The refactored code with complete pointer sanitization and TOCTOU protection."

PROMPT_HARDWARE="TASK FOR MODULE: Hardware Drivers & Interrupts (Keyboard, Timer, Serial, RTC)
Analyze the attached hardware interaction classes for microkernel suitability and interrupt safety:
1. Interrupt Reentrance: Verify if the IRQ handlers lock shared data structures thread-safely via SpinLocks.
2. Bottom-Half Processing: Identify code in the IRQ handlers that takes too long to execute and defer it.
3. Microkernel Migration: Prepare the code to delegate hardware access to a userspace service via a HAL.

OUTPUT REQUIRED:
- A list of latency drivers within the IRQ handlers.
- The refactored code featuring cleanly separated Top-Half / Bottom-Half interrupt structures."

# ==============================================================================
# Logic & Execution
# ==============================================================================

print_usage() {
    echo "Usage: $0 <module_type> <source_file>"
    echo "Available modules: memory, task, ipc, boundary, hardware"
    echo "Example: $0 task src/scheduler.cpp"
}

if [ "$#" -ne 2 ]; then
    print_usage
    exit 1
fi

MODULE_TYPE=$1
SOURCE_FILE=$2

if [ ! -f "$SOURCE_FILE" ]; then
    echo "Error: Source file '$SOURCE_FILE' not found."
    exit 1
fi

case "$MODULE_TYPE" in
    memory)   SELECTED_PROMPT="$PROMPT_MEMORY" ;;
    task)     SELECTED_PROMPT="$PROMPT_TASK" ;;
    ipc)      SELECTED_PROMPT="$PROMPT_IPC" ;;
    boundary) SELECTED_PROMPT="$PROMPT_BOUNDARY" ;;
    hardware) SELECTED_PROMPT="$PROMPT_HARDWARE" ;;
    *) echo "Error: Unknown module '$MODULE_TYPE'."; print_usage; exit 1 ;;
esac

# ------------------------------------------------------------------------------
# Output Directory and File Setup
# ------------------------------------------------------------------------------
AUDIT_DIR="audits"
mkdir -p "$AUDIT_DIR"

# Extract filename without extension (e.g. "src/scheduler.cpp" -> "scheduler")
RAW_FILENAME=$(basename "$SOURCE_FILE")
MODULE_NAME="${RAW_FILENAME%.*}"
OUTPUT_FILE="${AUDIT_DIR}/${MODULE_NAME}.md"

# ------------------------------------------------------------------------------
# Auto-Detect Header File (.hpp or .h)
# ------------------------------------------------------------------------------
BASE_PATH="${SOURCE_FILE%.*}"
HEADER_FILE=""
HEADER_PAYLOAD=""

if [ -f "${BASE_PATH}.hpp" ]; then
    HEADER_FILE="${BASE_PATH}.hpp"
elif [ -f "${BASE_PATH}.h" ]; then
    HEADER_FILE="${BASE_PATH}.h"
fi

if [ -n "$HEADER_FILE" ]; then
    echo "--> Found matching header: $HEADER_FILE"
    HEADER_CODE=$(cat "$HEADER_FILE")
    HEADER_PAYLOAD="--- HEADER FILE BEGIN (${HEADER_FILE}) ---
${HEADER_CODE}
--- HEADER FILE END ---

"
else
    echo "--> No matching header (.hpp or .h) found next to the source file. Proceeding without it."
fi

# Read the source code safely
SOURCE_CODE=$(cat "$SOURCE_FILE")

# Combine the instruction, header (if found), and source code for the user message
USER_MESSAGE="${SELECTED_PROMPT}

${HEADER_PAYLOAD}--- SOURCE CODE BEGIN (${SOURCE_FILE}) ---
${SOURCE_CODE}
--- SOURCE CODE END ---"

# Build the JSON Payload safely using jq
echo "--> Building JSON payload and communicating with OpenRouter ($MODEL)..."
JSON_PAYLOAD=$(jq -n \
  --arg model "$MODEL" \
  --arg system_msg "$SYSTEM_CONTEXT" \
  --arg user_msg "$USER_MESSAGE" \
  '{
    "model": $model,
    "messages": [
      {
        "role": "system",
        "content": $system_msg
      },
      {
        "role": "user",
        "content": $user_msg
      }
    ]
  }')

# Execute the API Call
RESPONSE=$(curl -s -X POST "https://openrouter.ai/api/v1/chat/completions" \
  -H "Authorization: Bearer $OPENROUTER_API_KEY" \
  -H "Content-Type: application/json" \
  -H "HTTP-Referer: https://github.com/rtos-project" \
  -H "X-Title: RTOS-Review-Script" \
  -d "$JSON_PAYLOAD")

# Check if curl failed completely
if [ $? -ne 0 ]; then
    echo "Error: Failed to connect to OpenRouter API."
    exit 1
fi

# Extract the AI's response using jq
AI_CONTENT=$(echo "$RESPONSE" | jq -r '.choices[0].message.content')

# If AI_CONTENT is null, something went wrong
if [ "$AI_CONTENT" = "null" ] || [ -z "$AI_CONTENT" ]; then
    echo "Error: API returned an invalid response. Raw output:"
    echo "$RESPONSE" | jq .
    exit 1
fi

# Save the output directly into the audits/ directory
echo "$AI_CONTENT" > "$OUTPUT_FILE"

echo "================================================================================"
echo "✅ Audit report successfully saved to: $OUTPUT_FILE"
echo "================================================================================"
