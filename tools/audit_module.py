import os
import sys
import requests
import json

# OpenRouter API Key aus der Umgebungsvariable laden
OPENROUTER_API_KEY = os.environ.get("OPENROUTER_API_KEY")

def read_file(path):
    with open(path, 'r', encoding='utf-8') as f:
        return f.read()

def run_agent(prompt, system_instruction):
    if not OPENROUTER_API_KEY:
        print("Error: OPENROUTER_API_KEY environment variable not set.")
        sys.exit(1)

    url = "https://openrouter.ai/api/v1/chat/completions"
    headers = {
        "Authorization": f"Bearer {OPENROUTER_API_KEY}",
        "Content-Type": "application/json",
        # OpenRouter bittet um diese optionalen Header für das Ranking
        "HTTP-Referer": "https://github.com/arnold-hasshold/jarvis-rtos", 
        "X-Title": "Jarvis RTOS Code Auditor"
    }
    
    # Hier konfigurieren wir das Modell für OpenRouter
    data = {
        "model": "anthropic/claude-3.5-sonnet", # OpenRouter-spezifischer Modell-Pfad
        "temperature": 0.1,
        "messages": [
            {"role": "system", "content": system_instruction},
            {"role": "user", "content": prompt}
        ]
    }

    response = requests.post(url, headers=headers, data=json.dumps(data))
    
    if response.status_code != 200:
        print(f"API Error ({response.status_code}): {response.text}")
        sys.exit(1)
        
    response_json = response.json()
    return response_json['choices'][0]['message']['content']

def main():
    if len(sys.argv) < 3:
        print("Usage: python3 tools/audit_module.py <file.hpp> <file.cpp>")
        sys.exit(1)

    hpp_path = sys.argv[1]
    cpp_path = sys.argv[2]
    
    code_content = f"--- HEADER FILE ({hpp_path}) ---\n{read_file(hpp_path)}\n\n--- IMPLEMENTATION FILE ({cpp_path}) ---\n{read_file(cpp_path)}"
    
    print(f"[*] Starting customized Jarvis architectural audit via OpenRouter for {cpp_path}...")

    # AGENT 1: Jarvis Context & Variable Isolation
    print("[>] Running Agent 1: Environmental Isolation...")
    sys_1 = (
        "You are an isolation agent for the Jarvis RTOS freestanding kernel (C++20).\n"
        "Your task is to identify and list every macro (CONFIG_*), every type of locking primitive,\n"
        "and every raw pointer usage in the provided source files."
    )
    prompt_1 = f"Identify all configuration flags, locking primitives, and raw pointer usages in this code:\n\n{code_content}"
    result_1 = run_agent(prompt_1, sys_1)

    # AGENT 2: The Ruthless Hard-RT Attacker
    print("[>] Running Agent 2: Safety & Invariant Attacker (ASIL-D Rules)...")
    sys_2 = (
        "You are a critical system safety auditor for Jarvis RTOS (Hard Real-Time, ASIL-D level).\n"
        "Your sole mission is to find architectural violations. You must flag code that violates these strict invariants:\n"
        "1. DEPRECATED GLOBAL IRQGUARD: The use of global 'IrqGuard' is strictly forbidden. Code must use fine-grained 'SpinLock' + 'SpinLockGuard' or 'sync::Mutex' without IrqGuard.\n"
        "2. RAW POINTERS VS REFERENCES: Task blocks (TCB) and IPC endpoints must pass by reference to prevent dangling lookups and compiler stack slot reuse optimizations from corrupting data under -O3.\n"
        "3. WCET VIOLATIONS: Loops must have bounded iterations (no open-ended while loops in hot paths).\n"
        "4. UNDEFINED BEHAVIOR: Guard hardware builtins (like __builtin_clzll) against zero inputs.\n"
        "Be extremely aggressive. If a violation is suspected, report it."
    )
    prompt_2 = (
        f"Analyze the code for architectural flaws.\n\n"
        f"--- CODE TO AUDIT ---\n{code_content}\n\n"
        f"--- CONTEXT FROM AGENT 1 ---\n{result_1}"
    )
    result_2 = run_agent(prompt_2, sys_2)

    # AGENT 3: Jarvis Core Synthesizer & Fixer
    print("[>] Running Agent 3: Kernel Synthesizer & C++20 Fixer...")
    sys_3 = (
        "You are the Lead Kernel Architect of Jarvis RTOS. You filter out false positives from the attacker's report\n"
        "and output a highly polished, professional audit report in Markdown format.\n"
        "For every genuine bug or invariant violation, you must provide the exact line analysis and a rock-solid,\n"
        "zero-allocation, freestanding C++20 code fix."
    )
    prompt_3 = (
        f"Review the attacker's findings against the original code. Filter out false positives, "
        f"and output the final Markdown report with precise code diffs for verified flaws.\n\n"
        f"Original Code:\n{code_content}\n\n"
        f"Attacker Findings:\n{result_2}"
    )
    final_report = run_agent(prompt_3, sys_3)

    # Bericht speichern
    report_name = f"audit_{os.path.basename(cpp_path)}.md"
    with open(report_name, 'w', encoding='utf-8') as f:
        f.write(final_report)
        
    print(f"[+] Audit complete! Report compiled into: {report_name}")

if __name__ == "__main__":
    main()
