#include <test.hpp>
#include <logger.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: After reschedule(), context-switch globals are set consistently.
// Input: Create two tasks, call reschedule(), inspect atomics.
// Expect: save_rsp_to points to current task, load_rsp_from points to next,
//         load_cr3_from is non-zero.
// Depends: Scheduler, context-switch atomics
JARVIS_TEST(atomic_globals_set_on_reschedule) {
    auto* task_a = TaskControlBlock::create([](){}, 5, 10);
    JARVIS_ASSERT(task_a != nullptr);
    Scheduler::add_task(*task_a);

    auto* task_b = TaskControlBlock::create([](){}, 5, 10);
    JARVIS_ASSERT(task_b != nullptr);
    Scheduler::add_task(*task_b);

    auto* original = Scheduler::current_task();

    Scheduler::set_current(*task_a);
    Scheduler::reschedule();

    uint64_t saved_rsp = __atomic_load_n(
        reinterpret_cast<uint64_t volatile*>(&kernel::scheduler_save_rsp_to),
        __ATOMIC_ACQUIRE);
    uint64_t loaded_rsp = __atomic_load_n(
        reinterpret_cast<uint64_t volatile*>(&kernel::scheduler_load_rsp_from),
        __ATOMIC_ACQUIRE);
    uint64_t loaded_cr3 = __atomic_load_n(
        reinterpret_cast<uint64_t volatile*>(&kernel::scheduler_load_cr3_from),
        __ATOMIC_ACQUIRE);

    JARVIS_ASSERT_FMT(saved_rsp == reinterpret_cast<uint64_t>(task_a),
                      "save_rsp_to = %lx (expected %lx)", saved_rsp,
                      reinterpret_cast<uint64_t>(task_a));
    JARVIS_ASSERT_FMT(loaded_rsp == reinterpret_cast<uint64_t>(task_b),
                      "load_rsp_from = %lx (expected %lx)", loaded_rsp,
                      reinterpret_cast<uint64_t>(task_b));
    JARVIS_ASSERT_FMT(loaded_cr3 != 0,
                      "load_cr3_from should be non-zero, got 0");

    Scheduler::set_current(*original);

    Scheduler::remove_task(*task_a);
    task_a->cleanup();
    delete task_a;
    Scheduler::remove_task(*task_b);
    task_b->cleanup();
    delete task_b;

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: fpu_owner can be read/written atomically.
// Input: Set fpu_owner to a task pointer, read it back.
// Expect: Value matches what was written.
// Depends: fpu_owner atomic
JARVIS_TEST(atomic_fpu_owner_read_write) {
    auto* old = __atomic_load_n(
        reinterpret_cast<TaskControlBlock* volatile*>(&kernel::fpu_owner),
        __ATOMIC_ACQUIRE);
    auto* test_ptr = reinterpret_cast<TaskControlBlock*>(uintptr_t(0xDEADBEEF));
    __atomic_store_n(
        reinterpret_cast<TaskControlBlock* volatile*>(&kernel::fpu_owner),
        test_ptr, __ATOMIC_RELEASE);
    auto* val = __atomic_load_n(
        reinterpret_cast<TaskControlBlock* volatile*>(&kernel::fpu_owner),
        __ATOMIC_ACQUIRE);
    JARVIS_ASSERT(val == test_ptr);
    __atomic_store_n(
        reinterpret_cast<TaskControlBlock* volatile*>(&kernel::fpu_owner),
        old, __ATOMIC_RELEASE);
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: scheduler_next_task_id increments atomically.
// Input: Save, store new, verify, restore.
// Expect: Atomic operations preserve the value.
// Depends: scheduler_next_task_id atomic
JARVIS_TEST(atomic_next_task_id_consistency) {
    uint64_t old = __atomic_load_n(
        reinterpret_cast<uint64_t volatile*>(&kernel::scheduler_next_task_id),
        __ATOMIC_ACQUIRE);
    __atomic_store_n(
        reinterpret_cast<uint64_t volatile*>(&kernel::scheduler_next_task_id),
        old + 100, __ATOMIC_RELEASE);
    uint64_t val = __atomic_load_n(
        reinterpret_cast<uint64_t volatile*>(&kernel::scheduler_next_task_id),
        __ATOMIC_ACQUIRE);
    JARVIS_ASSERT_EQ(old + 100, val);
    __atomic_store_n(
        reinterpret_cast<uint64_t volatile*>(&kernel::scheduler_next_task_id),
        old, __ATOMIC_RELEASE);
    JARVIS_TEST_PASS();
}

void register_atomic_context_switch_tests() {
    Logger::info("Registering atomic context-switch tests");
    JARVIS_REGISTER_TEST(atomic_globals_set_on_reschedule);
    JARVIS_REGISTER_TEST(atomic_fpu_owner_read_write);
    JARVIS_REGISTER_TEST(atomic_next_task_id_consistency);
}
