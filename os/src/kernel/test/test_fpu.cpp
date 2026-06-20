#include <test.hpp>
#include <logger.hpp>
#include <kernel/arch/cpuid.hpp>
#include <kernel/arch/io.hpp>
#include <kernel/task/scheduler.hpp>
#include <kernel/task/task.hpp>
#include <kernel/memory/mempool.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: CPUID reports FPU, FXSR, and SSE feature flags on x86_64
// Expect: All three bits should be set on any modern x86_64 CPU/emulator
JARVIS_TEST(fpu_cpuid_detection) {
    JARVIS_ASSERT_FMT(arch::has_fpu(), "CPUID leaf 1 EDX bit 0 (FPU) not set");
    JARVIS_ASSERT_FMT(arch::has_fxsr(), "CPUID leaf 1 EDX bit 24 (FXSR) not set");
    JARVIS_ASSERT_FMT(arch::has_sse(), "CPUID leaf 1 EDX bit 25 (SSE) not set");
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Executing a basic x87 FPU instruction via inline asm succeeds
// Expect: CR0.TS is cleared after the first FPU instruction; fpu_owner updated
JARVIS_TEST(fpu_basic_instruction) {
    uint64_t cr0_before = arch::read_cr0();
    bool ts_before = (cr0_before >> 3) & 1;

    asm volatile("finit" ::: "memory");

    uint64_t cr0_after = arch::read_cr0();
    bool ts_after = (cr0_after >> 3) & 1;

    JARVIS_ASSERT_FMT(!ts_after,
        "CR0.TS still set after FINIT (was %d before)", ts_before);

    auto* current = Scheduler::current_task();
    JARVIS_ASSERT(current != nullptr);
    JARVIS_ASSERT_FMT(__atomic_load_n(&fpu_owner, __ATOMIC_ACQUIRE) == current,
        "fpu_owner (%p) should be current task (%p)",
        (void*)fpu_owner, (void*)current);
    JARVIS_ASSERT_FMT(current->fpu_used,
        "current task fpu_used should be true after FINIT");

    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: The FXSAVE area captures non-zero state after an FPU instruction
// Input: Loads IEEE 754 1.0 (0x3FF0000000000000) into x87 ST0 via FLDL,
//        then FXSAVEs and checks the abridged tag word
// Expect: The saved tag byte shows ST0 as non-empty
JARVIS_TEST(fpu_fxsave_nonzero) {
    alignas(16) uint8_t buf[512];
    __builtin_memset(buf, 0, 512);

    // IEEE 754 double 1.0 = 0x3FF0000000000000
    uint64_t one = 0x3FF0000000000000ULL;

    asm volatile("finit\n"
                 "fldl %0" : : "m"(one));

    arch::fxsave(buf);

    // Abridged tag word at offset 4: bit 0 = ST0 tag (0=empty, 1=non-empty)
    JARVIS_ASSERT_FMT(buf[4] & 1,
        "FXSAVE tag byte (offset 4) was 0x%02x, expected bit 0 set", buf[4]);

    asm volatile("finit" ::: "memory");
    JARVIS_TEST_PASS();
}

// ── FPU context switch test (raw bit patterns, no C++ float types) ─────────

// IEEE 754 double 3.14159 ≈ 0x400921F9F01B866E
static constexpr uint64_t FPU_PI_BITS   = 0x400921F9F01B866EULL;
// IEEE 754 double 2.71828 ≈ 0x4005BF0A8B145769ULL
static constexpr uint64_t FPU_EULER_BITS = 0x4005BF0A8B145769ULL;

static volatile uint64_t g_fpu_val_result = 0;
static volatile uint64_t g_fpu_test_a_done = 0;
static volatile uint64_t g_fpu_test_b_done = 0;

static void fpu_task_a_entry() {
    uint64_t pi = FPU_PI_BITS;
    uint64_t result = 0;

    asm volatile("finit\n"
                 "fldl %0"           // pi → ST0
                 : : "m"(pi)
                 : "memory");

    g_fpu_test_a_done = 1;
    kernel::Scheduler::reschedule();

    // Read ST0 back as raw 64-bit IEEE 754 bits
    asm volatile("fstpl %0\n"       // ST0 → result, pop
                 "finit"
                 : "=m"(result)
                 :
                 : "st");

    g_fpu_val_result = result;
    g_fpu_test_a_done = 2;
}

static void fpu_task_b_entry() {
    while (!g_fpu_test_a_done) {
        kernel::Scheduler::reschedule();
    }

    // Load a different constant (euler) to trigger #NM and FPU switch
    uint64_t euler = FPU_EULER_BITS;
    asm volatile("finit\n"
                 "fldl %0\n"         // euler → ST0
                 "finit"             // discard
                 : : "m"(euler)
                 : "memory");

    g_fpu_test_b_done = 1;
}

// Runmode: kernel
// Testidea: Lazy FPU switch preserves FPU state across task boundaries
// Input: Creates task A (loads pi raw bits, yields), task B (loads euler),
//        then verifies task A's pi bits survive the round-trip
// Expect: Task A reads back the expected IEEE 754 bit pattern for 3.14159
JARVIS_TEST(fpu_context_switch) {
    g_fpu_val_result = 0;
    g_fpu_test_a_done = 0;
    g_fpu_test_b_done = 0;

    auto* task_a = TaskControlBlock::create(fpu_task_a_entry, 1, 10);
    auto* task_b = TaskControlBlock::create(fpu_task_b_entry, 2, 10);

    JARVIS_ASSERT(task_a != nullptr);
    JARVIS_ASSERT(task_b != nullptr);

    Scheduler::add_task(*task_a);
    Scheduler::add_task(*task_b);

    for (int i = 0; i < 200 && !g_fpu_test_b_done; ++i) {
        Scheduler::reschedule();
    }
    JARVIS_ASSERT_FMT(g_fpu_test_b_done,
        "Task B did not complete within 200 reschedule calls");

    for (int i = 0; i < 200 && g_fpu_test_a_done != 2; ++i) {
        Scheduler::reschedule();
    }
    JARVIS_ASSERT_FMT(g_fpu_test_a_done == 2,
        "Task A did not reach verification step (state=%llu)",
        (unsigned long long)g_fpu_test_a_done);

    uint64_t result = g_fpu_val_result;
    JARVIS_ASSERT_FMT(result != 0,
        "Task A FPU value was zeroed (context switch corruption)");
    JARVIS_ASSERT_FMT(result != ~0ULL,
        "Task A FPU value was all-ones (context switch corruption)");
    JARVIS_ASSERT_FMT(result == FPU_PI_BITS,
        "Task A FPU value 0x%016llx != expected 0x%016llx",
        (unsigned long long)result,
        (unsigned long long)FPU_PI_BITS);

    Scheduler::remove_task(*task_a);
    Scheduler::remove_task(*task_b);
    task_a->cleanup();
    task_b->cleanup();
    MemPool::free(task_a);
    MemPool::free(task_b);

    JARVIS_TEST_PASS();
}

void register_fpu_tests() {
    Logger::info("Registering FPU/SSE lazy context switch tests");
    JARVIS_REGISTER_TEST(fpu_cpuid_detection);
    JARVIS_REGISTER_TEST(fpu_basic_instruction);
    JARVIS_REGISTER_TEST(fpu_fxsave_nonzero);
    JARVIS_REGISTER_TEST(fpu_context_switch);
}
