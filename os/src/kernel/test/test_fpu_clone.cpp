#include <test.hpp>
#include <logger.hpp>
#include <constants.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/memory/mempool.hpp>

using namespace kernel;

static constexpr uint64_t FPU_PI_BITS = 0x400921F9F01B866EULL;

// Runmode: kernel
// Testidea: Cloning a task that owns the FPU copies the FXSAVE state to the child
// Input: Parent task uses x87 (finit + fldl pi), becomes fpu_owner; then clone
// Expect: child->fpu_used == true, child FXSAVE tag word shows ST0 non-empty
// Depends: TaskControlBlock::clone, kernel::Scheduler, arch::fxsave
JARVIS_TEST(fpu_clone_copies_state) {
    auto* parent = TaskControlBlock::create_user([](){}, 1, 10, 32_KiB);
    JARVIS_ASSERT(parent != nullptr);
    Scheduler::add_task(*parent);

    auto* original = Scheduler::current_task();
    Scheduler::set_current(*parent);

    uint64_t pi = FPU_PI_BITS;
    asm volatile("finit\nfldl %0" : : "m"(pi) : "memory");

    JARVIS_ASSERT_FMT(__atomic_load_n(&fpu_owner, __ATOMIC_ACQUIRE) == parent,
        "fpu_owner (%p) != parent (%p) after FINIT/FLDL",
        (void*)fpu_owner, (void*)parent);
    JARVIS_ASSERT_FMT(parent->fpu_used,
        "parent fpu_used not set after FINIT/FLDL");

    uint64_t regs[22] = {};
    regs[17] = 0x1000; regs[18] = arch::SEG_USER_CODE;
    regs[19] = arch::RFLAGS_DEFAULT;
    regs[20] = 0x80000000; regs[21] = arch::SEG_USER_DATA;

    auto* child = TaskControlBlock::clone(regs);
    JARVIS_ASSERT(child != nullptr);

    JARVIS_ASSERT_FMT(child->fpu_used,
        "child fpu_used not set after clone");

    // FXSAVE abridged tag word at offset 4: bit 0 = ST0 (1 = non-empty)
    JARVIS_ASSERT_FMT((child->fpu_state[4] & 1),
        "child fpu_state tag byte 0x%02x at offset 4, expected ST0 occupied",
        child->fpu_state[4]);

    // Parent state should also be saved (clone calls fxsave before copy)
    JARVIS_ASSERT_FMT((parent->fpu_state[4] & 1),
        "parent fpu_state tag byte 0x%02x at offset 4, expected ST0 occupied",
        parent->fpu_state[4]);

    // Clean up child (TCB allocated from MemPool via clone + alloc_id)
    child->cleanup();
    MemPool::free(child);

    Scheduler::remove_task(*parent);
    parent->cleanup();
    MemPool::free(parent);

    if (original) Scheduler::set_current(*original);

    // Re-init FPU to leave clean state for subsequent tests
    asm volatile("finit" ::: "memory");

    JARVIS_TEST_PASS();
}

void register_fpu_clone_tests() {
    Logger::info("Registering FPU clone tests");
    JARVIS_REGISTER_TEST(fpu_clone_copies_state);
}
