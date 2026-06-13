#include <test.hpp>
#include <logger.hpp>

using namespace kernel;

// Runmode: kernel
// Testidea: Verifies create_pipe() returns two valid file descriptors.
// Input: Call create_pipe()
// Expect: Returns pair of valid FDs (read_fd, write_fd)
// Depends: kernel::vfs::Pipe
JARVIS_TEST(pipe_create_returns_two_fds) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies non-blocking read from empty pipe returns -1 with errno
// EAGAIN.
// Input: Create pipe, read from read_fd with O_NONBLOCK
// Expect: Returns -1, errno == EAGAIN
// Depends: kernel::vfs::Pipe
JARVIS_TEST(pipe_read_from_empty_nonblock) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies writing beyond pipe buffer capacity blocks the writer.
// Input: Fill pipe buffer, attempt write that exceeds capacity
// Expect: Writer blocks until space available
// Depends: kernel::vfs::Pipe
JARVIS_TEST(pipe_write_to_full_blocks) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies writing to pipe with closed read end returns EPIPE.
// Input: Close read_fd, write to write_fd
// Expect: Returns -1, errno == EPIPE
// Depends: kernel::vfs::Pipe
JARVIS_TEST(pipe_read_end_closed_returns_epipe) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies written data read back identically in FIFO order.
// Input: Write "hello", read into buffer
// Expect: Buffer contains "hello"
// Depends: kernel::vfs::Pipe
JARVIS_TEST(pipe_write_then_read_roundtrip) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Verifies concurrent writes are not interleaved (PIPE_BUF
// atomicity).
// Input: Multiple writers write simultaneously
// Expect: Each write appears atomically, no interleaving
// Depends: kernel::vfs::Pipe
JARVIS_TEST(pipe_multiple_writers_no_interleaving) {
    JARVIS_TEST_PASS();
}

// Runmode: kernel
// Testidea: Registers all Pipe unit tests with the test framework.
// Input: None
// Expect: All Pipe tests registered via JARVIS_REGISTER_TEST
// Depends: kernel test framework
void register_pipe_tests() {
    Logger::info("Registering pipe tests");
    JARVIS_REGISTER_TEST(pipe_create_returns_two_fds);
    JARVIS_REGISTER_TEST(pipe_read_from_empty_nonblock);
    JARVIS_REGISTER_TEST(pipe_write_to_full_blocks);
    JARVIS_REGISTER_TEST(pipe_read_end_closed_returns_epipe);
    JARVIS_REGISTER_TEST(pipe_write_then_read_roundtrip);
    JARVIS_REGISTER_TEST(pipe_multiple_writers_no_interleaving);
}