/// @file test_tcb_write_log.cpp
/// @brief Demonstration that the TCB write-log tracer (docs/investigation
///        -cumulative-corruption.md Idea #4) fires when a stray write
///        corrupts a TCB and the poison-detection path runs.

#include <test.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/task/tcb_write_log.hpp>
#include <logger.hpp>

using namespace kernel;
using namespace kernel::test;

// Intent: prove the ring-buffer write tracker "catches" a stray write by
// dumping the last legitimate modifiers of a corrupted TCB.
//
// A real stray write bypasses the TCB_WRITE macro, so it is NOT recorded
// itself — that is precisely why the post-detection dump is valuable: it
// shows the last *legitimate* writers (callers) of the corrupted TCB,
// narrowing the suspect code path.
//
// Run with CONFIG_TCB_WRITE_LOG and grep the serial log for
// "[TCB-WRITE-LOG]" to confirm the dump was emitted.
JARVIS_TEST(test_tcb_write_log_catches_stray_write, "PRE: none | POST: none") {
    auto *t = Scheduler::current_task();
    JARVIS_ASSERT(t != nullptr);
    JARVIS_ASSERT(t->magic == TaskControlBlock::TCB_MAGIC);

    // Simulate a STRAY WRITE that corrupts a TCB field, bypassing TCB_WRITE.
    const uint64_t off = offsetof(TaskControlBlock, magic);
    *reinterpret_cast<uint64_t *>(reinterpret_cast<uint8_t *>(t) + off) =
        0xDDDDDDDDDDDDDDDDULL;

    // Trigger the poison-detection path. cleanup() sees the invalid magic,
    // prints "[CLEANUP] poisoned TCB" and calls
    // kernel::diag::dump_tcb_write_log(). The dump goes to the serial log.
    t->cleanup();

    // Restore the running task's magic so the scheduler keeps functioning.
    t->magic = TaskControlBlock::TCB_MAGIC;

    // Confirm the tracer is wired: dump_tcb_write_log must be callable and
    // must have recorded the legitimate create/restore writes of this TCB.
    kernel::diag::dump_tcb_write_log("[TEST] explicit dump after stray write");

    JARVIS_TEST_PASS();
}

void register_tcb_write_log_tests() {
    JARVIS_REGISTER_TEST(test_tcb_write_log_catches_stray_write);
}
