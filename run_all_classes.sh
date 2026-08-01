#!/usr/bin/env bash
# Run every test class (except meta/aggregate classes) separately under debug
# x86_64, record PASS/FAIL/STALL/WEDGE/HANG per class into test-history.txt.
# No debugging — only observe what works and what does not.
set -u
cd "$(dirname "$0")"

CLASSES="testrunner buffer_pool o1_scheduler random memory_safety memory_determinism lib hal_bits static_pools stack_profiler stack_alloc page_tables buffer_pool_deterministic no_op_new scheduler zombie_cleanup wcet_cleanup idle_cleanup deadlock lock_protocol timer wfg lock memory mempool checked_ptr pmm resource_exhaustion slab_reclaim ipc_blocking ipc_robustness ipc vfs process syscall arch vmm cross_arch device shell net security dmesg debug integration starvation_deadlock deadline_miss wcet_overrun ss_deadline deadline_recovery deadline_action wcet priority_inheritance stress init build sporadic atomic spinlock timing spinlock_stress apic_timer irq_alloc jitter threaded_irq gic plic preemption_under_syscall"

# Architecture-dependent classes: skip risc64 on non-riscv64 hosts
ARCH=$(uname -m)

TS="$(date '+%Y-%m-%d %H:%M:%S')"
LOGDIR=/tmp/jarvis_classruns
mkdir -p "$LOGDIR"

total=0; passed_classes=0; failed_classes=0
for c in $CLASSES; do
    total=$((total+1))
    pkill -9 -f qemu-system 2>/dev/null
    log="$LOGDIR/$c.log"
    start=$(date +%s)
    timeout 120 make execute-test x86_64 debug "$c" > "$log" 2>&1
    rc=$?
    end=$(date +%s)
    elapsed=$((end-start))
    npass=$(grep -cE ': PASS' "$log")
    nfail=$(grep -cE ': FAIL' "$log")
    nstall=$(grep -cE '\[STALL\]' "$log")
    nwedge=$(grep -cE '\[WEDGE\]' "$log")
    if grep -qE 'RESULT: TIMEOUT|Terminated: 15' "$log"; then hung=HANG; else hung=""; fi
    if [ "$rc" -ne 0 ] && [ -z "$hung" ]; then hung="ERR$rc"; fi

    # Determine class outcome
    if [ "$nfail" -eq 0 ] && [ "$nstall" -eq 0 ] && [ "$nwedge" -eq 0 ] && [ -z "$hung" ]; then
        verdict="OK"
        passed_classes=$((passed_classes+1))
    else
        verdict="FAIL"
        failed_classes=$((failed_classes+1))
    fi

    row="$TS class=$c PASS=$npass FAIL=$nfail STALL=$nstall WEDGE=$nwedge ${hung:-} TIME=${elapsed}s VERDICT=$verdict"
    printf '%s\n' "$row" >> test-history.txt
    printf '[%s] %s\n' "$verdict" "$row"
done

printf '\nSUMMARY: total=%d ok=%d fail=%d\n' "$total" "$passed_classes" "$failed_classes" >> test-history.txt
printf '\n=== SUMMARY total=%d ok=%d fail=%d ===\n' "$total" "$passed_classes" "$failed_classes"
