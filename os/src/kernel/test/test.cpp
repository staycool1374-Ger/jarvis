#include <kernel/test/test.hpp>
#include <kernel/memory/pmm.hpp>
#include <kernel/memory/mempool.hpp>
#include <kernel/ipc/ipc.hpp>
#include <string.hpp>
#include <utils.hpp>
#include <error.hpp>
#include <assert.hpp>

namespace kernel {
namespace test {

Test TestRegistry::tests_[MAX_TESTS] = {};
size_t TestRegistry::count_ = 0;
bool TestRegistry::initialized_ = false;

void TestRegistry::init() {
    if (initialized_) return;

    // --- string tests ---
    register_test("string.memset", "memset sets bytes correctly (large)", []() -> TestResult {
        uint8_t buf[64];
        memset(buf, 0xAB, 64);
        for (size_t i = 0; i < 64; ++i)
            if (buf[i] != 0xAB) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("string.memset_small", "memset with count < 16 (byte loop)", []() -> TestResult {
        uint8_t buf[12];
        memset(buf, 0x42, 12);
        for (size_t i = 0; i < 12; ++i)
            if (buf[i] != 0x42) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("string.memcpy", "memcpy copies bytes correctly", []() -> TestResult {
        const uint8_t src[32] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
                                 16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31};
        uint8_t dst[32];
        memset(dst, 0, 32);
        memcpy(dst, src, 32);
        for (size_t i = 0; i < 32; ++i)
            if (dst[i] != src[i]) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("string.memcpy_small", "memcpy with count < 16", []() -> TestResult {
        const uint8_t src[8] = {10,20,30,40,50,60,70,80};
        uint8_t dst[8] = {0};
        memcpy(dst, src, 8);
        for (size_t i = 0; i < 8; ++i)
            if (dst[i] != src[i]) return TestResult::FAIL;
        return TestResult::PASS;
    });

    register_test("string.memcmp_equal", "memcmp returns 0 for identical buffers", []() -> TestResult {
        uint8_t a[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
        uint8_t b[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
        TEST_ASSERT(memcmp(a, b, 16) == 0);
        return TestResult::PASS;
    });

    register_test("string.memcmp_diff", "memcmp returns non-zero for different buffers", []() -> TestResult {
        uint8_t a[8] = {1,2,3,4,5,6,7,8};
        uint8_t b[8] = {1,2,3,4,5,6,7,9};
        TEST_ASSERT(memcmp(a, b, 8) != 0);
        return TestResult::PASS;
    });

    register_test("string.strlen", "strlen returns correct length", []() -> TestResult {
        TEST_ASSERT(strlen("") == 0);
        TEST_ASSERT(strlen("a") == 1);
        TEST_ASSERT(strlen("hello") == 5);
        TEST_ASSERT(strlen("hello world!") == 12);
        return TestResult::PASS;
    });

    // --- utils tests ---
    register_test("utils.align_up", "align_up rounds up correctly", []() -> TestResult {
        TEST_ASSERT(align_up<uint64_t>(0, 4096) == 0);
        TEST_ASSERT(align_up<uint64_t>(1, 4096) == 4096);
        TEST_ASSERT(align_up<uint64_t>(4096, 4096) == 4096);
        TEST_ASSERT(align_up<uint64_t>(4097, 4096) == 8192);
        TEST_ASSERT(align_up<uint64_t>(15, 16) == 16);
        TEST_ASSERT(align_up<uint64_t>(16, 16) == 16);
        return TestResult::PASS;
    });

    register_test("utils.align_down", "align_down rounds down correctly", []() -> TestResult {
        TEST_ASSERT(align_down<uint64_t>(0, 4096) == 0);
        TEST_ASSERT(align_down<uint64_t>(4095, 4096) == 0);
        TEST_ASSERT(align_down<uint64_t>(4096, 4096) == 4096);
        TEST_ASSERT(align_down<uint64_t>(4097, 4096) == 4096);
        TEST_ASSERT(align_down<uint64_t>(15, 16) == 0);
        TEST_ASSERT(align_down<uint64_t>(16, 16) == 16);
        return TestResult::PASS;
    });

    // --- ErrorOr tests ---
    register_test("error_or.value", "ErrorOr with value returns ok", []() -> TestResult {
        ErrorOr<int> e(42);
        TEST_ASSERT(e.ok());
        TEST_ASSERT(*e == 42);
        return TestResult::PASS;
    });

    register_test("error_or.error", "ErrorOr with error returns !ok", []() -> TestResult {
        ErrorOr<int> e(Error::OOM);
        TEST_ASSERT(!e.ok());
        TEST_ASSERT(e.error == Error::OOM);
        return TestResult::PASS;
    });

    register_test("error_or.void_ok", "ErrorOr<void> default is ok", []() -> TestResult {
        ErrorOr<void> e;
        TEST_ASSERT(e.ok());
        return TestResult::PASS;
    });

    register_test("error_or.void_error", "ErrorOr<void> with error", []() -> TestResult {
        ErrorOr<void> e(Error::NOT_FOUND);
        TEST_ASSERT(!e.ok());
        TEST_ASSERT(e.error == Error::NOT_FOUND);
        return TestResult::PASS;
    });

    // --- PMM tests ---
    register_test("pmm.alloc_free", "PMM allocates and frees a single page", []() -> TestResult {
        uint64_t page = PMM::alloc_page();
        TEST_ASSERT(page != 0);
        TEST_ASSERT((page & 0xFFF) == 0); // page-aligned
        PMM::free_page(page);
        return TestResult::PASS;
    });

    register_test("pmm.alloc_contiguous", "PMM allocates contiguous pages", []() -> TestResult {
        uint64_t block = PMM::alloc_contiguous(4);
        TEST_ASSERT(block != 0);
        TEST_ASSERT((block & 0xFFF) == 0);
        PMM::free_page(block);
        PMM::free_page(block + 0x1000);
        PMM::free_page(block + 0x2000);
        PMM::free_page(block + 0x3000);
        return TestResult::PASS;
    });

    register_test("pmm.alloc_zero_size", "PMM alloc_contiguous with zero count returns 0", []() -> TestResult {
        TEST_ASSERT(PMM::alloc_contiguous(0) == 0);
        return TestResult::PASS;
    });

    // --- MemPool tests ---
    register_test("mempool.alloc_small", "MemPool allocates 16-byte block", []() -> TestResult {
        void* p = MemPool::alloc(16);
        TEST_ASSERT(p != nullptr);
        MemPool::free(p);
        return TestResult::PASS;
    });

    register_test("mempool.alloc_large", "MemPool allocates 1024-byte block", []() -> TestResult {
        void* p = MemPool::alloc(1024);
        TEST_ASSERT(p != nullptr);
        MemPool::free(p);
        return TestResult::PASS;
    });

    register_test("mempool.alloc_too_large", "MemPool returns null for >2048 bytes", []() -> TestResult {
        void* p = MemPool::alloc(4096);
        TEST_ASSERT(p == nullptr);
        return TestResult::PASS;
    });

    register_test("mempool.reuse", "MemPool reuses freed blocks", []() -> TestResult {
        void* p1 = MemPool::alloc(16);
        TEST_ASSERT(p1 != nullptr);
        MemPool::free(p1);
        void* p2 = MemPool::alloc(16);
        TEST_ASSERT(p2 != nullptr);
        TEST_ASSERT(p1 == p2);
        return TestResult::PASS;
    });

    // --- IPC tests ---
    register_test("ipc.create_mailbox", "IPC creates a mailbox", []() -> TestResult {
        auto* mb = IPC::create_mailbox(42);
        TEST_ASSERT(mb != nullptr);
        TEST_ASSERT(mb->owner_id == 42);
        TEST_ASSERT(mb->count == 0);
        IPC::destroy_mailbox(42);
        return TestResult::PASS;
    });

    register_test("ipc.send_receive", "IPC sends and receives a message", []() -> TestResult {
        auto* mb = IPC::create_mailbox(1);
        TEST_ASSERT(mb != nullptr);

        Message msg;
        msg.sender_id = 2;
        msg.type = 99;
        msg.data_size = 4;
        msg.data[0] = 0xDE;
        msg.data[1] = 0xAD;
        msg.data[2] = 0xBE;
        msg.data[3] = 0xEF;

        TEST_ASSERT(IPC::send(1, msg));

        Message recv;
        TEST_ASSERT(IPC::receive(1, recv));
        TEST_ASSERT(recv.sender_id == 2);
        TEST_ASSERT(recv.type == 99);
        TEST_ASSERT(recv.data_size == 4);
        TEST_ASSERT(recv.data[0] == 0xDE);
        TEST_ASSERT(recv.data[3] == 0xEF);

        IPC::destroy_mailbox(1);
        return TestResult::PASS;
    });

    register_test("ipc.mailbox_full", "IPC returns false when mailbox is full", []() -> TestResult {
        auto* mb = IPC::create_mailbox(7);
        TEST_ASSERT(mb != nullptr);

        Message msg;
        msg.sender_id = 0;
        msg.type = 0;
        msg.data_size = 0;

        bool all_sent = true;
        for (size_t i = 0; i < IPC_MAX_MAILBOX_MSG; ++i) {
            if (!IPC::send(7, msg)) { all_sent = false; break; }
        }
        TEST_ASSERT(all_sent);

        bool should_fail = IPC::send(7, msg);
        TEST_ASSERT(!should_fail);

        for (size_t i = 0; i < IPC_MAX_MAILBOX_MSG; ++i) {
            Message r;
            IPC::receive(7, r);
        }

        IPC::destroy_mailbox(7);
        return TestResult::PASS;
    });

    initialized_ = true;
}

bool TestRegistry::register_test(const char* name, const char* description,
                                 TestResult (*func)()) {
    if (count_ >= MAX_TESTS) return false;
    tests_[count_].name = name;
    tests_[count_].description = description;
    tests_[count_].func = func;
    tests_[count_].result = TestResult::SKIP;
    tests_[count_].failure_msg = nullptr;
    ++count_;
    return true;
}

const Test* TestRegistry::find(const char* name) {
    for (size_t i = 0; i < count_; ++i) {
        const char* tn = tests_[i].name;
        const char* n = name;
        while (*tn && *n && *tn == *n) { ++tn; ++n; }
        if (*tn == '\0' && *n == '\0') return &tests_[i];
    }
    return nullptr;
}

void TestRegistry::run_test(Test& t) {
    t.result = t.func();
}

TestReport TestRegistry::run_all() {
    TestReport report = {0, 0, 0, 0};
    for (size_t i = 0; i < count_; ++i) {
        run_test(tests_[i]);
        ++report.total;
        switch (tests_[i].result) {
        case TestResult::PASS:  ++report.passed;  break;
        case TestResult::FAIL:  ++report.failed;  break;
        case TestResult::SKIP:  ++report.skipped; break;
        }
    }
    return report;
}

TestReport TestRegistry::run(const char* name) {
    TestReport report = {0, 0, 0, 0};
    for (size_t i = 0; i < count_; ++i) {
        const char* tn = tests_[i].name;
        const char* n = name;
        while (*tn && *n && *tn == *n) { ++tn; ++n; }
        if (*tn != '\0' || *n != '\0') continue;

        run_test(tests_[i]);
        ++report.total;
        switch (tests_[i].result) {
        case TestResult::PASS:  ++report.passed;  break;
        case TestResult::FAIL:  ++report.failed;  break;
        case TestResult::SKIP:  ++report.skipped; break;
        }
        break;
    }
    return report;
}

} // namespace test
} // namespace kernel
